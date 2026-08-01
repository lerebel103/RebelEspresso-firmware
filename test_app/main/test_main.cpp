/**
 * Test application entry point.
 *
 * This app runs inside QEMU and executes all Unity test cases registered
 * by the test components. After all tests complete, it prints the results
 * and halts (QEMU will capture the output).
 */
#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <unity.h>

// Globals expected by test code (mirroring what main.cpp provides)
EventGroupHandle_t status_event_group;

static void run_tests(void *arg) {
    // Small delay to let system settle
    vTaskDelay(pdMS_TO_TICKS(100));

    printf("\n\n--- Running Unity Tests ---\n\n");

    // Run all registered TEST_CASE tests
    unity_run_all_tests();

    printf("\n--- Tests Complete ---\n");
    fflush(stdout);

    // In QEMU, exit after tests
    vTaskDelay(pdMS_TO_TICKS(1000));
    abort();
}

extern "C" void app_main() {
    // Initialize NVS — required by components that persist config
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Create the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create global event group used by components
    status_event_group = xEventGroupCreate();

    // Run tests in a task with sufficient stack
    xTaskCreate(run_tests, "tests", 8192, NULL, 5, NULL);
}
