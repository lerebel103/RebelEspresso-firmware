#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <version.h>
#include <cstring>
#include <csignal>
#include <esp_event.h>
#include <hap.h>
#include <src/events.h>
#include <hap_apple_chars.h>
#include <hap_apple_servs.h>
#include <src/thing_info.h>
#include <hw_config.h>
#include <esp_log.h>
#include "power.h"
#include "rtds.h"
#include "brew_temp.h"
#include "boiler_temp.h"


static hap_serv_t *service;

static hap_char_t* hc_brew_temp = nullptr;
static hap_char_t* hc_boiler_temp = nullptr;
static hap_char_t *hc_internal_temp = nullptr;
static hap_char_t* hc_cur_duty = nullptr;

#define TAG "hk"

#define BREW_NAME "Brew"

#define SWITCH_TASK_PRIORITY  4
#define HK_MAIN_STACK_SIZE (3 * 1024)
#define HK_TASK_NAME      "homekit"

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
static int _char_write(hap_write_data_t *write_data, int count,
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

static float _round_temp(measure_t result) {
  float temp = (float) (result.fault == (uint8_t) RTD_NoError ? result.value : 24);
  return int(temp * 10) / 10.0f;
}

static int _char_read(hap_char_t *hc, hap_status_t *status_code,
                          void *serv_priv, void *read_priv) {
  int ret = HAP_SUCCESS;
  hap_val_t new_val;
  struct measure_t result{};

  if (hc == NULL) {
    ret = HAP_FAIL;
    goto error;
  }

  if ( hc == hc_boiler_temp) {
    rtds_get(&result, RTD_BREW_BOILER_IDX);
    new_val.f = _round_temp(result);
    hap_char_update_val(hc, &new_val);
    *status_code = HAP_STATUS_SUCCESS;
  } else if (hc == hc_brew_temp) {
    // Brew temperature
    rtds_get(&result, RTD_BREW_HEAD_IDX);
    new_val.f = _round_temp(result);
    hap_char_update_val(hc, &new_val);
    *status_code = HAP_STATUS_SUCCESS;
  } else if (hc == hc_internal_temp) {
    rtds_get(&result, RTD_INTERNAL_IDX);
    new_val.f = _round_temp(result);
    hap_char_update_val(hc, &new_val);
    *status_code = HAP_STATUS_SUCCESS;
  } else if (hc == hc_cur_duty) {
    new_val.f = boiler_temp_get_duty();
    hap_char_update_val(hc, &new_val);
    *status_code = HAP_STATUS_SUCCESS;
  } else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_NAME)) {
    new_val.s = (char *) BREW_NAME;
    hap_char_update_val(hc, &new_val);
    *status_code = HAP_STATUS_SUCCESS;
  } else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_CURRENT_HEATING_COOLING_STATE) ||
             !strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE)) {
    new_val.i = (power_is_active() ? 1 : 0);
    hap_char_update_val(hc, &new_val);
    *status_code = HAP_STATUS_SUCCESS;
  } else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_TARGET_TEMPERATURE)) {
    new_val.f = (float) brew_temp_get_setpoint();
    if (new_val.f < BREW_TEMP_MIN) {
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
  } else {
    ESP_LOGE(TAG, "Unknown characteristic %s requested for Brew", hap_char_get_type_uuid(hc));
    goto error;
  }

  return ret;

  error:
  *status_code = HAP_STATUS_RES_ABSENT;
  return ret;
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
}


static void _tick_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
  if (id != TICK) {
    return;
  }

  uint64_t now = 0;
  static uint64_t last_metrics = 0;
  static uint64_t last_power = 0;

  if (event_data != nullptr) {
    now = *(uint64_t *) event_data;
  }

  // Then send
  ESP_LOGD(TAG, "Sending new temperatures");

  hap_status_t status_code;

  if (now - last_metrics > 2e6) {
    _char_read(hc_brew_temp, &status_code, nullptr, nullptr);
    _char_read(hc_boiler_temp, &status_code, nullptr, nullptr);
    _char_read(hc_internal_temp, &status_code, nullptr, nullptr);
    _char_read(hc_cur_duty, &status_code, nullptr, nullptr);


    last_metrics = now;
  }

  // refresh power state if we missed the event
  // Homekit connects much later in the piece, and we would have missed events if connection goes down.
  if (now - last_power > 5e6) {
    _power_events(nullptr, MACHINE_EVENTS, power_is_active() ? POWER_ACTIVE : POWER_STANDBY, nullptr);

    last_power = now;
  }
}

/*The main thread for handling the RebelEspresso Switch Accessory */
static void espresso_thread_entry(void *arg) {
  struct measure_t result{};
  float brew_temp, boiler_temp, setpoint;

  hap_char_t *hc = nullptr;
  int ret = HAP_SUCCESS;
  int  cur_duty;
  float internal_temp;
  hap_acc_t *accessory;
  uint8_t product_data[] = {'E', 'S', 'P', '3', '2', 'H', 'A', 'P'};

  /* Initialize the HAP core */
  hap_init(HAP_TRANSPORT_WIFI);

  /* Initialise the mandatory parameters for Accessory which will be added as
   * the mandatory services internally
   */
  hap_acc_cfg_t cfg = {
      .name = (char *) "RebelEspresso",
      .model = (char *) THING_TYPE,
      .manufacturer = (char *) "LeRebel",
      .serial_num = (char *) (thing_info_id()),
      .fw_rev = (char *) FIRMWARE_VERSION,
      .hw_rev = (char *) HARDWARE_REVISION_MAJOR,
      .pv = (char *) "1.1.0",
      .cid = HAP_CID_OTHER,
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
  brew_temp = (result.fault == (uint8_t) RTD_NoError ? result.value : 21);

  /* Create the RebelEspresso Switch Service. Include the "name" since this is a user visible service  */
  setpoint = brew_temp_get_setpoint();
  if (setpoint < BREW_TEMP_MIN) {
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

  ret = hap_serv_add_char(service, hap_char_name_create((char *) "Brew"));
  if (ret != HAP_SUCCESS) {
    ESP_LOGE(TAG, "Failed to add optional characteristics to Switch");
    goto switch_err;
  }

  // Adjust limits
  hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_CURRENT_HEATING_COOLING_STATE);
  hap_char_int_set_constraints(hc, 0, 1, 1);
  hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE);
  hap_char_int_set_constraints(hc, 0, 1, 1);
  hc_brew_temp = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
  hap_char_float_set_constraints(hc_brew_temp, 0, 150, 0.1);
  hc = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_TARGET_TEMPERATURE);
  hap_char_float_set_constraints(hc, BREW_TEMP_MIN, BREW_TEMP_MAX, 0.5);
  hap_serv_set_write_cb(service, _char_write);
  hap_serv_set_read_cb(service, _char_read);
  hap_acc_add_serv(accessory, service);

  // ---
  rtds_get(&result, RTD_BREW_BOILER_IDX);
  boiler_temp = result.fault == RTD_NoError ? (float)result.value : 0;
  hc = hap_serv_temperature_sensor_create(boiler_temp);
  hap_serv_add_char(hc, hap_char_name_create((char *) "Boiler"));
  hc_boiler_temp = hap_serv_get_char_by_uuid(hc, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
  hap_char_float_set_constraints(hc_boiler_temp, 0.0, 150.0, 0.1);
  hap_acc_add_serv(accessory, hc);

  rtds_get(&result, RTD_INTERNAL_IDX);
  internal_temp = result.fault == RTD_NoError ? (float)result.value : 0;
  hc = hap_serv_temperature_sensor_create(internal_temp);
  hap_serv_add_char(hc, hap_char_name_create((char *) "Internal"));
  hc_internal_temp = hap_serv_get_char_by_uuid(hc, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
  hap_acc_add_serv(accessory, hc);

  cur_duty = boiler_temp_get_duty();
  hc = hap_serv_temperature_sensor_create( cur_duty);
  hap_serv_add_char(hc, hap_char_name_create((char *) "Boiler Duty"));
  hc_cur_duty = hap_serv_get_char_by_uuid(hc, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
  //hap_char_int_set_constraints(hc_cur_duty, 0, 100, 0.1);
  hap_acc_add_serv(accessory, hc);


  /* Add the Accessory to the HomeKit Database */
  hap_add_accessory(accessory);
  
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
  ESP_ERROR_CHECK(esp_event_handler_unregister(MACHINE_EVENTS, POWER_STANDBY, _power_events));
  ESP_ERROR_CHECK(esp_event_handler_unregister(MACHINE_EVENTS, POWER_ACTIVE, _power_events));
  ESP_ERROR_CHECK(esp_event_handler_unregister(MACHINE_EVENTS, TICK, _tick_events));

  hap_stop();
}

void homekit_init() {


  // Register power events so we can send to home kit
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, POWER_STANDBY,
                                             _power_events, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, POWER_ACTIVE,
                                             _power_events, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, TICK,
                                             _tick_events, nullptr));

  xTaskCreate(espresso_thread_entry, HK_TASK_NAME, HK_MAIN_STACK_SIZE,
              NULL, SWITCH_TASK_PRIORITY, NULL);

}
