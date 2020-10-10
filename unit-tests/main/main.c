#include <stdio.h>
#include <cJSON.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <unity.h>
#include <unity_config.h>
#include <esp_event.h>
#include <src/events.h>

esp_event_loop_handle_t g_event_loop;
EventGroupHandle_t status_event_group;

void unityTask(void *pvParameters)
{
    vTaskDelay(2); /* Delay a bit to let the main task be deleted */


    /**
     * cJSON seems to do some static allocations that ruin memory leak tracing.
     * So we do blank calls here to get these allocs to take place out of the test
     * cases once and for all.
     */
    cJSON* config = cJSON_CreateObject();
    cJSON_AddNumberToObject(config, "randomiser_sec", 1234.8);
    char *json = cJSON_PrintUnformatted(config);
    printf("cJSON leaks masked %s\n", json);
    cJSON_free(json);
    cJSON_Delete(config);

    // Create global dependencies
    esp_event_loop_args_t event_loop_args = {
            .queue_size = 5,
            .task_name = "App Event Loop", // No task will be created
            .task_priority = uxTaskPriorityGet(NULL),
            .task_stack_size = 2548,
            .task_core_id = tskNO_AFFINITY
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&event_loop_args, &g_event_loop));
    status_event_group = xEventGroupCreate();

    unity_run_menu(); /* Doesn't return */
}

void app_main()
{
    // Note: if unpinning this task, change the way run times are calculated in
    // unity_platform
    xTaskCreatePinnedToCore(unityTask, "unityTask", 6*1024, NULL, 5, NULL, 0);
}
