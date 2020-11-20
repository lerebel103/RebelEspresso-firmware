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
#include "iot.h"
#include "controller.h"

#define TAG "iot"

#define CONTROL_LOOP_PERIOD 1000
#define IOT_SEND_INTERVAL 5000

static esp_event_loop_handle_t s_event_loop;
TickType_t g_last_iot_send = 0;
static TickType_t s_last_status_update_tick = 0;
static bool s_ota_needed = true;
static bool _go = true;


static void send_iot_events(TickType_t tick) {
    if (tick >= (g_last_iot_send + IOT_SEND_INTERVAL)) {

        g_last_iot_send = tick;
    }
}

void _iot_task(void*) {
    while (_go) {
        time_t time_millis = xTaskGetTickCount() * portTICK_PERIOD_MS;

        wifi_tick(time_millis);
        homekit_tick(time_millis);

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
            send_iot_events(time_millis);

            // Do we need to run OTA (wait 15 seconds after we connect to let things settle first)?
            if (s_ota_needed && ota_is_enabled() && (time_millis - mqtt_last_connect_attempt()) > 15000) {
                ota_run();
                s_ota_needed = false;
            }
        } else {
            // This will trigger another ota check again if MQTT drops out, bit of a hack really
            s_ota_needed = true;
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

    /*
    mqtt_set_client_private_key(    "-----BEGIN EC PRIVATE KEY-----\n"
                                    "MHcCAQEEIDvKD7cTp5i6OeJhXvw/PxQFWs0rq5wAt3hTOUScpJr1oAoGCCqGSM49\n"
                                    "AwEHoUQDQgAE3a5tg30Yse9WDVIzNYI5p9AXB9ipSBMLg1/yv6fweoNikB+/mbtg\n"
                                    "55cJUWmK2ZbxLvlwh19Exe4DVZNfZVL6og==\n"
                                    "-----END EC PRIVATE KEY-----"
    );

    mqtt_set_registry_id("RebelEspresso");
    mqtt_set_location("asia-east1");
    mqtt_set_project_id("rebelthings");
     */

    ota_init(thing_info_id(), THING_TYPE, FIRMWARE_VERSION, HARDWARE_REVISION);

    mqtt_set_cfg_cb(controller_handle_new_cfg);
    mqtt_init();
    homekit_init();

    // We also start a secondary tick loop, which for a machine wide tick that is not realtime based
    xTaskCreate(_iot_task, "iot task", configMINIMAL_STACK_SIZE + 2048, nullptr, 5, nullptr);
}

