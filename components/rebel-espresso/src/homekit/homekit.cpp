#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <version.h>
#include <string.h>
#include <signal.h>
#include <esp_event.h>
#include <hap.h>
#include <src/events.h>
#include <soc_log.h>
#include <hap_apple_chars.h>
#include <hap_apple_servs.h>
#include <src/thing_info.h>
#include <src/control/power.h>
#include <src/hw/rtds.h>

static bool requestedFactoryReset = false;
static bool clearPairings = false;
static esp_event_loop_handle_t s_event_loop;
static bool s_init = false;
static hap_serv_t *service;


#define TAG "hk"

#define SWITCH_TASK_PRIORITY  4
#define SWITCH_TASK_STACKSIZE 4 * 1024
#define SWITCH_TASK_NAME      "hap_switch"


static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    // Send new power state as it happens
    hap_char_t *hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_ON);
    if (hc != NULL) {
        ESP_LOGI(TAG, "Sending new power state");
        hap_val_t new_val;

        if (id == POWER_STANDBY) {
            new_val.b = false;
        } else if (id == POWER_ACTIVE) {
            new_val.b = true;
        }

        hap_char_update_val(hc, &new_val);
    }
}

/**
 * @brief The network reset button callback handler.
 * Useful for testing the Wi-Fi re-configuration feature of WAC2
 */
static void reset_network_handler(void* arg)
{
    hap_reset_network();
}
/**
 * @brief The factory reset button callback handler.
 */
static void reset_to_factory_handler(void* arg)
{
    hap_reset_to_factory();
}

/**
 * The Reset button  GPIO initialisation function.
 * Same button will be used for resetting Wi-Fi network as well as for reset to factory based on
 * the time for which the button is pressed.
 */
static void reset_key_init(uint32_t key_gpio_pin)
{
    //button_handle_t handle = iot_button_create(key_gpio_pin, BUTTON_ACTIVE_LOW);
    //iot_button_add_on_release_cb(handle, RESET_NETWORK_BUTTON_TIMEOUT, reset_network_handler, NULL);
    //iot_button_add_on_press_cb(handle, RESET_TO_FACTORY_BUTTON_TIMEOUT, reset_to_factory_handler, NULL);
}

/* Mandatory identify routine for the accessory.
 * In a real accessory, something like LED blink should be implemented
 * got visual identification
 */
static int switch_identify(hap_acc_t *ha)
{
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

/* Callback for handling writes on the RebelEspresso Switch Service
 */
static int switch_write(hap_write_data_t write_data[], int count,
                           void *serv_priv, void *write_priv)
{
    int i, ret = HAP_SUCCESS;
    hap_write_data_t *write;
    for (i = 0; i < count; i++) {
        write = &write_data[i];
        /* Setting a default error value */
        *(write->status) = HAP_STATUS_VAL_INVALID;
        if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ON)) {
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

static int switch_read(hap_char_t *hc, hap_status_t *status_code,
                       void *serv_priv, void *read_priv) {
    int ret = HAP_SUCCESS;

    if (hc == NULL) {
        ret = HAP_FAIL;
        goto error;
    }

    if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_ON)) {
        ESP_LOGD(TAG, "******* Reading Power state");
        hap_val_t new_val;
        new_val.b = power_is_active();
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS;
    /*} else if( !strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_CURRENT_TEMPERATURE) ) {
        ESP_LOGI(TAG, "******* Reading Current temperature");
        struct rtd_data_t result{};
        rtds_get(&result, 0);
        double temp = (result.fault == Max31865Error::NoError ? result.temperature : 21);

        hap_val_t new_val;
        new_val.f = (float)temp;
        hap_char_update_val(hc, &new_val);
        *status_code = HAP_STATUS_SUCCESS; */
    }

    return ret;

    error:
    *status_code = HAP_STATUS_RES_ABSENT;
    return ret;
}

/*The main thread for handling the RebelEspresso Switch Accessory */
static void switch_thread_entry(void *arg)
{
    struct rtd_data_t result{};
    double temp;
    int ret = HAP_SUCCESS;
    hap_acc_t *accessory;
    uint8_t product_data[] = {'E','S','P','3','2','H','A','P'};

    /* Initialize the HAP core */
    hap_init(HAP_TRANSPORT_WIFI);

    /* Initialise the mandatory parameters for Accessory which will be added as
     * the mandatory services internally
     */
    hap_acc_cfg_t cfg = {
            .name = (char*)"RebelEspresso",
            .model = (char*)THING_TYPE,
            .manufacturer = (char*)"LeRebel",
            .serial_num = (char*)(thing_info_id()),
            .fw_rev = (char*)FIRMWARE_VERSION,
            .hw_rev = (char*)HARDWARE_REVISION,
            .pv = (char*)"1.1.0",
            .cid = HAP_CID_OTHER,
            .identify_routine = switch_identify,
    };

    /* Create accessory object */
    accessory = hap_acc_create(&cfg);
    if (!accessory) {
        ESP_LOGE(TAG, "Failed to create accessory");
        goto switch_err;
    }

    /* Add a dummy Product Data */
    hap_acc_add_product_data(accessory, product_data, sizeof(product_data));

    /* Create the RebelEspresso Switch Service. Include the "name" since this is a user visible service  */
    service = hap_serv_switch_create(false);
    if (!service) {
        ESP_LOGE(TAG, "Failed to create RebelEspresso Service");
        goto switch_err;
    }

    /* Add the optional characteristic to the Light Bulb Service */
    rtds_get(&result, 0);
    temp = (result.fault == Max31865Error::NoError ? result.temperature : 21);
    ret = hap_serv_add_char(service, hap_char_name_create("Coffee Machine"));
    //ret |= hap_serv_add_char(service, hap_char_current_temperature_create((float)temp));

    if (ret != HAP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to add optional characteristics to Switch");
        goto switch_err;
    }


    /* Set the write callback for the service */
    hap_serv_set_write_cb(service, switch_write);
    hap_serv_set_read_cb(service, switch_read);

    /* Add the RebelEspresso Switch Service to the Accessory Object */
    hap_acc_add_serv(accessory, service);

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

    hap_stop();

    // Wait for HAP to terminate - gah this is bad coding indeed
    while(s_init) {
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

    xTaskCreate(switch_thread_entry, SWITCH_TASK_NAME, SWITCH_TASK_STACKSIZE,
                NULL, SWITCH_TASK_PRIORITY, NULL);

    s_init = true;
}

bool homekit_is_initialised() {
    return s_init;
}
