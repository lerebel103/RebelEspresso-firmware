#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <version.h>
#include <cstring>
#include <csignal>
#include <esp_event.h>
#include <hap.h>
#include <src/events.h>
#include <soc_log.h>
#include <hap_apple_chars.h>
#include <hap_apple_servs.h>
#include <src/thing_info.h>
#include <hw_config.h>
#include "power.h"
#include "rtds.h"
#include "brew_temp.h"

static esp_event_loop_handle_t s_event_loop;
static bool s_init = false;

static hap_serv_t *service;
static hap_serv_t *s_boiler_service;

#define TAG "hk"

#define BREW_NAME "Brew"
#define BOILER_NAME "Boiler"

#define SWITCH_TASK_PRIORITY  4
#define SWITCH_TASK_STACKSIZE 4 * 1024
#define SWITCH_TASK_NAME      "hap_switch"

#define BREW_TEMP_MIN 88
#define BREW_TEMP_MAX 94


/* Mandatory identify routine for the accessory.
 * In a real accessory, something like LED blink should be implemented
 * got visual identification
 */
static int device_identify(hap_acc_t *ha) {
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

/* Callback for handling writes on the RebelEspresso Switch Service
 */
static int brew_char_write(hap_write_data_t *write_data, int count,
                           void *serv_priv, void *write_priv) {
    int i, ret = HAP_SUCCESS;
    hap_write_data_t *write;
    for (i = 0; i < count; i++) {
        write = &write_data[i];
        /* Setting a default error value */
        *(write->status) = HAP_STATUS_VAL_INVALID;
        if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE)) {
            ESP_LOGD(TAG, "Received Write for RebelEspresso %s", write->val.b ? "On" : "Off");
            if (write->val.b) {
                power_active();
                if (power_is_active()) {
                    *(write->status) = HAP_STATUS_SUCCESS;
                } else {
                    *(write->status) = HAP_STATUS_RES_BUSY;
                }
            } else {
                power_standby();
                if (!power_is_active()) {
                    *(write->status) = HAP_STATUS_SUCCESS;
                } else {
                    *(write->status) = HAP_STATUS_RES_BUSY;
                }
            }
        } else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_TARGET_TEMPERATURE)) {
            ESP_LOGD(TAG, "Received Write for Target Temp %f", write->val.f);
            brew_temp_set_setpoint(write->val.f);
            *(write->status) = HAP_STATUS_SUCCESS;
        } else {
            *(write->status) = HAP_STATUS_RES_ABSENT;
        }
        /* If the characteristic write was successful, update it in hap core
         */
        if (*(write->status) == HAP_STATUS_SUCCESS) {
            hap_char_update_val(write->hc, &(write->val));
        } else {
            /* Else, set the return value appropriately to report error */
            ret = HAP_FAIL;
        }
    }
    return ret;
}

static int brew_char_read(hap_char_t *hc, hap_status_t *status_code,
                          void *serv_priv, void *read_priv) {
    int ret = HAP_SUCCESS;
    hap_val_t new_val;
    struct reading_t result{};

    if (hc == NULL) {
        ret = HAP_FAIL;
        goto error;
    }

    // Brew temperature
    rtds_get(&result, RTD_BREW_HEAD_IDX);

    if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_NAME)) {
        new_val.s = (char *) BREW_NAME;
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    } else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_CURRENT_HEATING_COOLING_STATE) ||
               !strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE)) {
        new_val.i = (power_is_active() ? 1 : 0);
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    } else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_CURRENT_TEMPERATURE)) {
        new_val.f = (float) (result.fault == (uint8_t)RTD_NoError ? result.value : 21);
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    } else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_TARGET_TEMPERATURE)) {
        new_val.f = (float) brew_temp_get_setpoint();
        if(new_val.f < BREW_TEMP_MIN) {
            new_val.f = BREW_TEMP_MIN;
        }
        if (new_val.f > BREW_TEMP_MAX) {
            new_val.f = BREW_TEMP_MAX;
        }
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    } else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_TEMPERATURE_DISPLAY_UNITS)) {
        if (rtds_get_unit() == UNIT_CELCIUS) {
            new_val.i = 0;
        } else {
            new_val.i = 1;
        }
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    } else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_STATUS_FAULT)) {
        new_val.i = (result.fault == (uint8_t)RTD_NoError ? 0 : 1);
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    } else {
        ESP_LOGE(TAG, "Unknown characteristic %s requested for Brew", hap_char_get_type_uuid(hc));
        goto error;
    }

    return ret;

    error:
    *status_code = HAP_STATUS_RES_ABSENT;
    return ret;
}

static int boiler_char_read(hap_char_t *hc, hap_status_t *status_code,
                            void *serv_priv, void *read_priv) {
    int ret = HAP_SUCCESS;
    hap_val_t new_val;
    struct reading_t result{};

    if (hc == NULL) {
        ret = HAP_FAIL;
        goto error;
    }

    // Boiler temperature
    rtds_get(&result, 0);

    if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_NAME)) {
        new_val.s = (char *) BOILER_NAME;
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    } else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_CURRENT_TEMPERATURE)) {
        double temp = (result.fault == (uint8_t)RTD_NoError ? result.value : 21);

        new_val.f = (float) temp;
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    } else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_STATUS_FAULT)) {
        new_val.i = (result.fault == (uint8_t)RTD_NoError ? 0 : 1);
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    } else {
        ESP_LOGE(TAG, "Unknown characteristic %s requested for Boiler", hap_char_get_type_uuid(hc));
        goto error;
    }

    return ret;

    error:
    *status_code = HAP_STATUS_RES_ABSENT;
    return ret;
}

static void _tick_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id != TICK) {
        return;
    }

    uint64_t now = 0;
    static uint64_t last_send = 0;
    if (event_data != nullptr) {
        now = *(uint64_t *) event_data;
    }

    if (now - last_send > 2e6) {
        // Then send
        ESP_LOGD(TAG, "Sending new temperatures");

        hap_status_t status_code;

        hap_char_t *hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
        brew_char_read(hc, &status_code, nullptr, nullptr);
        //hc = hap_serv_get_char_by_uuid(s_boiler_service, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
        //boiler_char_read(hc, &status_code, nullptr, nullptr);

        last_send = now;
    }
}


static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    // Send new power state as it happens
    ESP_LOGI(TAG, "Sending new power state");
    hap_val_t new_val;

    if (id == POWER_STANDBY) {
        new_val.u = 0;
    } else if (id == POWER_ACTIVE) {
        new_val.u = 1;
    }

    hap_char_t *hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE);
    if (hc != NULL) {
        hap_char_update_val(hc, &new_val);
    }
    hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_CURRENT_HEATING_COOLING_STATE);
    if (hc != NULL) {
        hap_char_update_val(hc, &new_val);
    }


    //hap_reset_pairings();
}


/*The main thread for handling the RebelEspresso Switch Accessory */
static void switch_thread_entry(void *arg) {
    struct reading_t result{};
    double brew_temp, setpoint;
    //double boiler_temp;

    hap_char_t *hc = nullptr;
    int ret = HAP_SUCCESS;
    hap_acc_t *accessory;
    uint8_t product_data[] = {'E', 'S', 'P', '3', '2', 'H', 'A', 'P'};

    /* Initialize the HAP core */
    hap_init(HAP_TRANSPORT_WIFI);

    /* Initialise the mandatory parameters for Accessory which will be added as
     * the mandatory services internally
     */
    hap_acc_cfg_t cfg = {
            .name = (char *) "Espresso",
            .model = (char *) THING_TYPE,
            .manufacturer = (char *) "LeRebel",
            .serial_num = (char *) (thing_info_id()),
            .fw_rev = (char *) FIRMWARE_VERSION,
            .hw_rev = (char *) HARDWARE_REVISION,
            .pv = (char *) "1.1.0",
            .cid = HAP_CID_SWITCH,
            .identify_routine = device_identify,
    };

    /* Create accessory object */
    accessory = hap_acc_create(&cfg);
    if (!accessory) {
        ESP_LOGE(TAG, "Failed to create accessory");
        goto switch_err;
    }

    /* Add a dummy Product Data */
    hap_acc_add_product_data(accessory, product_data, sizeof(product_data));

    rtds_get(&result, RTD_BREW_HEAD_IDX);
    brew_temp = (result.fault == (uint8_t)RTD_NoError ? result.value : 21);

    /* Create the RebelEspresso Switch Service. Include the "name" since this is a user visible service  */
    setpoint = brew_temp_get_setpoint();
    if(setpoint < BREW_TEMP_MIN) {
        setpoint = BREW_TEMP_MIN;
    }
    if (setpoint > BREW_TEMP_MAX) {
        setpoint = BREW_TEMP_MAX;
    }

    service = hap_serv_thermostat_create(
            power_is_active(),
            power_is_active(),
            (float) brew_temp,
            (float) setpoint,
            rtds_get_unit() == UNIT_CELCIUS ? 0 : 1
    );
    if (!service) {
        ESP_LOGE(TAG, "Failed to create RebelEspresso Service");
        goto switch_err;
    }

    /* Add the optional characteristic to the Light Bulb Service */
    ret = hap_serv_add_char(service, hap_char_name_create((char *) "Brew"));
    ret |= hap_serv_add_char(service,
                             hap_char_status_fault_create((result.fault == (uint8_t)RTD_NoError ? 0 : 1)));

    if (ret != HAP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to add optional characteristics to Switch");
        goto switch_err;
    }

    // Adjust limits
    hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_CURRENT_HEATING_COOLING_STATE);
    hap_char_int_set_constraints(hc, 0, 1, 1);
    hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE);
    hap_char_int_set_constraints(hc, 0, 1, 1);
    hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    hap_char_float_set_constraints(hc, 0, 180, 0.1);
    hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_TARGET_TEMPERATURE);
    hap_char_float_set_constraints(hc, BREW_TEMP_MIN, BREW_TEMP_MAX, 1);

    hap_serv_set_write_cb(service, brew_char_write);
    hap_serv_set_read_cb(service, brew_char_read);
    hap_acc_add_serv(accessory, service);

    /*// Now for boiler
    rtds_get(&result, 0);
    boiler_temp = (result.fault == RTD_NoError ? result.temperature : 0);
    s_boiler_service = hap_serv_temperature_sensor_create((float) boiler_temp);
    ret = hap_serv_add_char(s_boiler_service, hap_char_name_create((char *) "Boiler"));
    ret |= hap_serv_add_char(s_boiler_service,
                             hap_char_status_fault_create((result.fault == RTD_NoError ? 0 : 1)));

    if (ret != HAP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to add optional characteristics to Switch");
        goto switch_err;
    }

    hc = hap_serv_get_char_by_uuid(s_boiler_service, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    hap_char_float_set_constraints(hc, 0, 180, 0.1);

    hap_serv_set_read_cb(s_boiler_service, boiler_char_read);
    hap_acc_add_serv(accessory, s_boiler_service);
     */


#ifdef CONFIG_FIRMWARE_SERVICE
    /*  Required for server verification during OTA, PEM format as string  */
    static char server_cert[] = {};
    hap_fw_upgrade_config_t ota_config = {
        .server_cert_pem = server_cert,
    };
    /* Create and add the Firmware Upgrade Service, if enabled.
     * Please refer the FW Upgrade documentation under components/homekit/extras/include/hap_fw_upgrade.h
     * and the top level README for more information.
     */
    service = hap_serv_fw_upgrade_create(&ota_config);
    if (!service) {
        ESP_LOGE(TAG, "Failed to create Firmware Upgrade Service");
        goto switch_err;
    }
    hap_acc_add_serv(accessory, service);
#endif

    /* Add the Accessory to the HomeKit Database */
    hap_add_accessory(accessory);

    /* Initialize the RebelEspresso Switch Hardware */
    //switch_init();

    /* Register a common button for reset Wi-Fi network and reset to factory.
     */
    //reset_key_init(RESET_GPIO);

    /* TODO: Do the actual hardware initialization here */

    /* For production accessories, the setup code shouldn't be programmed on to
     * the device. Instead, the setup info, derived from the setup code must
     * be used. Use the factory_nvs_gen utility to generate this data and then
     * flash it into the factory NVS partition.
     *
     * By default, the setup ID and setup info will be read from the factory_nvs
     * Flash partition and so, is not required to set here explicitly.
     *
     * However, for testing purpose, this can be overridden by using hap_set_setup_code()
     * and hap_set_setup_id() APIs, as has been done here.
     */
#ifdef CONFIG_EXAMPLE_USE_HARDCODED_SETUP_CODE
    /* Unique Setup code of the format xxx-xx-xxx. Default: 111-22-333 */
    hap_set_setup_code(CONFIG_EXAMPLE_SETUP_CODE);
    /* Unique four character Setup Id. Default: ES32 */
    hap_set_setup_id(CONFIG_EXAMPLE_SETUP_ID);
#ifdef CONFIG_APP_WIFI_USE_WAC_PROVISIONING
    app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, true, cfg.cid);
#else
    app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, false, cfg.cid);
#endif
#endif

    /* Enable Hardware MFi authentication (applicable only for MFi variant of SDK) */
    hap_enable_mfi_auth(HAP_MFI_AUTH_HW);

    /* After all the initializations are done, start the HAP core */
    hap_start();

    /* The task ends here. The read/write callbacks will be invoked by the HAP Framework */
    vTaskDelete(NULL);

    switch_err:
    hap_acc_delete(accessory);
    vTaskDelete(NULL);
}


void homekit_terminate() {
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, TICK, _tick_events));

    hap_stop();

    // Wait for HAP to terminate - gah this is bad coding indeed
    while (s_init) {
        vTaskDelay(10);
    }
}

void homekit_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;

    // Register power events so we can send to home kit
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, TICK,
                                                    _tick_events, s_event_loop));

    xTaskCreate(switch_thread_entry, SWITCH_TASK_NAME, SWITCH_TASK_STACKSIZE,
                NULL, SWITCH_TASK_PRIORITY, NULL);

    s_init = true;
}

bool homekit_is_initialised() {
    return s_init;
}
