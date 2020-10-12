
extern "C" {
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
}

#include <sys/ota.h>
#include <esp_event.h>
#include <Max31865.h>

#include "hw_config.h"
#include "state.h"
#include "control/controller.h"
#include "thing_info.h"
#include "sys/nvram_store.h"
#include "sys/wifi_connect.h"
#include "sys/sntp.h"
#include "sys/mqtt.h"
#include "sys/homekit.h"

#include "hw/oled/display.h"

#define TAG  "main"

// Event group pointer so we get system events to sync up
EventGroupHandle_t status_event_group;


extern "C" void app_main() {
    esp_event_loop_args_t event_loop_args = {
            .queue_size = 5,
            .task_name = "App Event Loop", // No task will be created
            .task_priority = uxTaskPriorityGet(NULL),
            .task_stack_size = 2548,
            .task_core_id = tskNO_AFFINITY
    };
    esp_event_loop_handle_t event_loop;
    ESP_ERROR_CHECK(esp_event_loop_create(&event_loop_args, &event_loop));
    status_event_group = xEventGroupCreate();

    esp_log_level_set("gpio", ESP_LOG_ERROR);

    // Do core initialisations first
    nvram_store_init();
    store_inc_cycle_count(); // Record number of power cycles.

    thing_info_init();
    state_print_system_info();
    controller_init(event_loop);
    display_init();

    // Now for wifi, ota, mqtt
    wifi_init();

    wifi_set_ssid("ortyma");
    wifi_set_password("pho3nixlerebel103");

    ota_init(thing_info_id(), THING_TYPE, FIRMWARE_VERSION, HARDWARE_REVISION);


    mqtt_set_client_private_key(    "-----BEGIN EC PRIVATE KEY-----\n"
                                    "MHcCAQEEIDvKD7cTp5i6OeJhXvw/PxQFWs0rq5wAt3hTOUScpJr1oAoGCCqGSM49\n"
                                    "AwEHoUQDQgAE3a5tg30Yse9WDVIzNYI5p9AXB9ipSBMLg1/yv6fweoNikB+/mbtg\n"
                                    "55cJUWmK2ZbxLvlwh19Exe4DVZNfZVL6og==\n"
                                    "-----END EC PRIVATE KEY-----"
    );

    mqtt_set_registry_id("RebelEspresso");
    mqtt_set_location("asia-east1");
    mqtt_set_project_id("rebelthings");


    ota_init(thing_info_id(), THING_TYPE, FIRMWARE_VERSION, HARDWARE_REVISION);
    //mqtt_set_ota_cfg_cb(ota_cfg_from_json);
    mqtt_set_cfg_cb(controller_handle_new_cfg);
    mqtt_init();
    homekit_init();


    // Here's our control loop
    controller_enter_loop();
    esp_event_loop_delete(event_loop);
}


