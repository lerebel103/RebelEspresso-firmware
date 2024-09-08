#include <src/thing_info.h>
#include <_generated/version.h>
#include <src/homekit/homekit.h>
#include <freertos/task.h>
#include <src/events.h>
#include <esp_log.h>
#include <ctime>
#include <src/state.h>
#include <hw_config.h>
#include "iot.h"
#include "controller.h"
#include "rtds.h"
#include "boiler_temp.h"
#include "power.h"

#define TAG "iot"

#define CONTROL_LOOP_PERIOD         1000
#define IOT_SEND_INTERVAL_ACTIVE    1000
#define IOT_SEND_INTERVAL_INACTIVE  60000


TickType_t g_last_iot_send = 0;
static TickType_t s_last_status_update_tick = 0;
static bool _go = true;


static void send_iot_events(TickType_t tick, int send_interval_msec) {
  // Careful here, we do static allocations so we don't fragment the heap over time
  static cJSON *root = cJSON_CreateObject();
  static cJSON *timestamp_elm = cJSON_AddNumberToObject(root, "timestamp", 0);
  static cJSON *internal_temp_elm = cJSON_AddNumberToObject(root, "internal_temp", 0);
  static cJSON *boiler_temp_elm = cJSON_AddNumberToObject(root, "boiler_temp", 0);
  static cJSON *boiler_setpoint_elm = cJSON_AddNumberToObject(root, "boiler_setpoint", 0);
  static cJSON *boiler_heat_duty_elm = cJSON_AddNumberToObject(root, "boiler_heat_duty", 0);
  static cJSON *brew_temp_elm = cJSON_AddNumberToObject(root, "brew_temp", 0);
  static cJSON *aux_temp_elm = cJSON_AddNumberToObject(root, "aux_temp", 0);

  if (tick >= (g_last_iot_send + send_interval_msec)) {
    g_last_iot_send = tick;

    struct measure_t data = {};
    char buf[256];

    // Update fields now
    cJSON_SetNumberValue(timestamp_elm, time(NULL));

    rtds_get(&data, RTD_INTERNAL_IDX);
    cJSON_SetNumberValue(internal_temp_elm, data.value);

    rtds_get(&data, RTD_BREW_BOILER_IDX);
    cJSON_SetNumberValue(boiler_temp_elm, data.value);

    cJSON_SetNumberValue(boiler_setpoint_elm, boiler_temp_get_current_setpoint());

    cJSON_SetNumberValue(boiler_heat_duty_elm, boiler_temp_get_duty());

    rtds_get(&data, RTD_BREW_HEAD_IDX);
    cJSON_SetNumberValue(brew_temp_elm, data.value);

    rtds_get(&data, RTD_STEAM_BOILER_IDX);
    cJSON_SetNumberValue(aux_temp_elm, data.value);

    cJSON_PrintPreallocated(root, buf, 256, false);
    // mqtt_send_telemetry(buf);
  }
}

void iot_process_events() {
  while (_go) {
    time_t time_millis = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Send MQTT stuff as required
    if (xEventGroupGetBits(status_event_group) & CORE_MQTT_CLIENT_CONNECTED_BIT) {

      auto send_state = xEventGroupGetBits(status_event_group) & SEND_STATE_BIT;
      if (send_state && (time_millis - s_last_status_update_tick) > 10000) {
        // No earlier than 10s for Google IoT
        ESP_LOGI(TAG, "Sending new state");
        s_last_status_update_tick = time_millis;
        state_send(time(NULL));
        xEventGroupClearBits(status_event_group, SEND_STATE_BIT);
      }

      send_iot_events(time_millis, (power_is_active() ? IOT_SEND_INTERVAL_ACTIVE : IOT_SEND_INTERVAL_INACTIVE));
    }

    // Approximately every second...
    time_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (CONTROL_LOOP_PERIOD > (now - time_millis)) {
      // Run event loop dispatch
      vTaskDelay((CONTROL_LOOP_PERIOD - (now - time_millis)) / portTICK_PERIOD_MS);
    }
  }
}

void iot_init() {
  homekit_init();
}

