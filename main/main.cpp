
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
#include <src/sys/sys_reset.h>
#include "state.h"
#include "control/controller.h"
#include "thing_info.h"
#include "sys/nvram_store.h"

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

    // force NVS partition delete
    nvram_store_init();
    store_inc_cycle_count(); // Record number of power cycles.

    thing_info_init();
    display_init(event_loop);
    state_print_system_info();
    controller_init(event_loop);


    // Here's our control loop
    controller_enter_loop();
    esp_event_loop_delete(event_loop);
}


