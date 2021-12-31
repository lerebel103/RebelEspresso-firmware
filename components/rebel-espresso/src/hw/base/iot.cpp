#include <src/sys/wifi_connect.h>
#include <src/thing_info.h>
#include <_generated/version.h>
#include <src/sys/mqtt.h>
#include <src/homekit/homekit.h>
#include <src/sys/ota.h>
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

static esp_event_loop_handle_t s_event_loop;
TickType_t g_last_iot_send = 0;
static TickType_t s_last_status_update_tick = 0;
static bool _go = true;


static void send_iot_events(TickType_t tick, int send_interval_msec) {
    // Careful here, we do static allocations so we don't fragment the heap over time
    static cJSON *root = cJSON_CreateObject();
    static cJSON *timestamp_elm = cJSON_AddNumberToObject(root, "timestamp", 0);
    static cJSON *boiler_temp_elm = cJSON_AddNumberToObject(root, "boiler_temp", 0);
    static cJSON *boiler_setpoint_elm = cJSON_AddNumberToObject(root, "boiler_setpoint", 0);
    static cJSON *boiler_heat_duty_elm = cJSON_AddNumberToObject(root, "boiler_heat_duty", 0);
    static cJSON *brew_temp_elm = cJSON_AddNumberToObject(root, "brew_temp", 0);
    static cJSON *aux_temp_elm = cJSON_AddNumberToObject(root, "aux_temp", 0);

    if (tick >= (g_last_iot_send + send_interval_msec)) {
        g_last_iot_send = tick;

        struct reading_t data = {};
        char buf[256];

        // Update fields now
        cJSON_SetNumberValue(timestamp_elm, time(NULL));

        rtds_get(&data, RTD_BREW_BOILER_IDX);
        cJSON_SetNumberValue(boiler_temp_elm, data.value);

        cJSON_SetNumberValue(boiler_setpoint_elm, boiler_temp_get_current_setpoint());

        cJSON_SetNumberValue(boiler_heat_duty_elm, boiler_temp_get_duty());

        rtds_get(&data, RTD_BREW_HEAD_IDX);
        cJSON_SetNumberValue(brew_temp_elm, data.value);

        rtds_get(&data, RTD_STEAM_BOILER_IDX);
        cJSON_SetNumberValue(aux_temp_elm, data.value);

        cJSON_PrintPreallocated(root, buf, 256, false);
        mqtt_send_telemetry(buf);
    }
}

void _iot_task(void *) {
    uint32_t ota_count = 0;
    bool is_comms_up = false;

    while (_go) {
        time_t time_millis = xTaskGetTickCount() * portTICK_PERIOD_MS;

        wifi_tick(time_millis);

        if (xEventGroupGetBits(status_event_group) & TIME_SYNC_BIT) {
            if (ota_count == 0) {
                ota_run();
                ota_count++;
            } else if (!ota_is_running() && !is_comms_up) {
                mqtt_init();
                homekit_init(s_event_loop);
                is_comms_up = true;
            }
        }

        // Send MQTT stuff as required
        if (xEventGroupGetBits(status_event_group) & MQTT_CONNECTED_BIT) {
            auto send_state = xEventGroupGetBits(status_event_group) & SEND_STATE_BIT;
            if (send_state && (time_millis - s_last_status_update_tick) > 10000) {
                // No earlier than 10s for Google IoT
                ESP_LOGI(TAG, "Sending new state");
                s_last_status_update_tick = time_millis;
                state_send(time(NULL));
                xEventGroupClearBits(status_event_group, SEND_STATE_BIT);
            }

            send_iot_events(time_millis, (power_is_active() ? IOT_SEND_INTERVAL_ACTIVE : IOT_SEND_INTERVAL_INACTIVE));

            // Ok, se we assume OTA did not brick this device if we got here
            ota_check_pending_validate_end();
        }

        // Approximately every second...
        time_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (CONTROL_LOOP_PERIOD > (now - time_millis)) {
            // Run event loop dispatch
            vTaskDelay((CONTROL_LOOP_PERIOD - (now - time_millis)) / portTICK_PERIOD_MS);
        }
    }

}

void iot_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;

    // Now for wifi, ota, mqtt

    wifi_init();
    ota_init(thing_info_id(), THING_TYPE, FIRMWARE_VERSION, HARDWARE_REVISION);
    mqtt_set_cfg_cb(controller_handle_new_cfg);

    // We also start a secondary tick loop, which for a machine wide tick that is not realtime based
    xTaskCreate(_iot_task, "iot task", configMINIMAL_STACK_SIZE + 2048, nullptr, 5, nullptr);
}

