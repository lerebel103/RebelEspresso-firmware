
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
    //display_init();

    // Now for witi, ota, mqtt
    wifi_init();
    ota_init(thing_info_id(), THING_TYPE, FIRMWARE_VERSION, HARDWARE_REVISION);

    mqtt_set_ota_cfg_cb(ota_cfg_from_json);
    //mqtt_set_controller_cfg_cb(controller_cfg_from_json);


    mqtt_init();
    homekit_init();


/*    auto tempSensor = Max31865(GPIO_MISO, GPIO_MOSI, GPIO_SCK, GPIO_RTD_CS);
    max31865_config_t tempConfig = {};
    tempConfig.autoConversion = true;
    tempConfig.vbias = true;
    tempConfig.filter = Max31865Filter::Hz50;
    tempConfig.nWires = Max31865NWires::Three;
    max31865_rtd_config_t rtdConfig = {};
    rtdConfig.nominal = 100.0f;
    rtdConfig.ref = 4000.0f;
    ESP_ERROR_CHECK(tempSensor.begin(tempConfig));
    ESP_ERROR_CHECK(tempSensor.setRTDThresholds(0x2000, 0x2500));


    while (true) {
        uint16_t rtd;
        Max31865Error fault = Max31865Error::NoError;
        tempSensor.getRTD(&rtd, &fault);
        float temp = Max31865::RTDtoTemperature(rtd, rtdConfig);
        ESP_LOGI("Temperature", "%.2f C, fault: %d", temp, (int)fault);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
*/


    // Here's our control loop
    controller_enter_loop();
    esp_event_loop_delete(event_loop);
}


