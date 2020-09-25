#include "process_loop.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_intr_alloc.h>
#include <hal/timer_types.h>
#include <driver/timer.h>
#include <driver/gpio.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>
#include <cmath>
#include <sys/time.h>
#include <hw/rtds.h>

#define TAG "process"
#define TIMER_DIVIDER         16  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds

#define TIMER_INTERVAL0_SEC   ( 1.0 )

static timer_idx_t s_timer_idx = TIMER_0;
static timer_group_t s_timer_group = TIMER_GROUP_0;
static TaskHandle_t _task_handle = nullptr;
static bool _go = false;
static SemaphoreHandle_t s_semaphore = NULL;

/**
 * Timer interrupt handler that drives our process loop
 * @param para
 */
static void IRAM_ATTR _process_loop_isr(void *para) {
    // Re-enable timer
    timer_group_clr_intr_status_in_isr(s_timer_group, s_timer_idx);

    static BaseType_t xHigherPriorityTaskWoken;

    xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_semaphore, &xHigherPriorityTaskWoken);

    /* If xHigherPriorityTaskWoken was set to true you
    we should yield.  The actual macro used here is
    port specific. */
    if (xHigherPriorityTaskWoken != pdFALSE) {
        portYIELD_FROM_ISR();
    }
}

static void _tick(time_t timestamp) {
    ESP_LOGI(TAG, "Process, heap: %d, min: %d", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());

    // Read system state
    rtd_data_t data;

    rtds_read_1(&data);
    ESP_LOGI(TAG, "RTD1 %f", data.temperature);
    rtds_read_2(&data);
    ESP_LOGI(TAG, "RTD2 %f", data.temperature);
    rtds_read_3(&data);
    ESP_LOGI(TAG, "RTD3 %f", data.temperature);
    rtds_read_4(&data);
    ESP_LOGI(TAG, "RTD4 %f", data.temperature);
}

static void _process_task(void *) {
    ESP_LOGI(TAG, "Process loop starting");
    // We want a strict watchdog timer on this one
    esp_task_wdt_init(ceil(TIMER_INTERVAL0_SEC * 1.5), true);
    esp_task_wdt_add(nullptr);

    do {
        if (xSemaphoreTake(s_semaphore, portMAX_DELAY) == pdTRUE) {
            // Do it
            _tick(time(NULL));

            // Done, reset ISR to go again and maintain watchdog timer
            esp_task_wdt_reset();
            timer_group_enable_alarm_in_isr(s_timer_group, s_timer_idx);
        }
    } while (_go);
    ESP_LOGI(TAG, "Process loop ended");

    // Kill resources
    _task_handle = nullptr;
    vTaskDelete(nullptr);
}


void process_loop_suspend() {

}

void process_loop_resume() {

}


void process_loop_init() {
    s_semaphore = xSemaphoreCreateBinary();

    gpio_install_isr_service(
            ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_LEVEL3);

    /* Select and initialize basic parameters of the timer */
    timer_config_t config = {
            .alarm_en = TIMER_ALARM_EN,
            .counter_en = TIMER_START,
            .intr_type = TIMER_INTR_LEVEL,
            .counter_dir = TIMER_COUNT_UP,
            .auto_reload = TIMER_AUTORELOAD_EN,
            .divider = TIMER_DIVIDER,
    }; // default clock source is APB

    ESP_LOGI(TAG, "Configuring process timer");

    ESP_ERROR_CHECK(timer_init(s_timer_group, s_timer_idx, &config));

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    ESP_ERROR_CHECK(timer_set_counter_value(s_timer_group, s_timer_idx, 0x00000000ULL));

    /* Configure the alarm value and the interrupt on alarm. */
    ESP_ERROR_CHECK(timer_set_alarm_value(s_timer_group, s_timer_idx, (TIMER_INTERVAL0_SEC) * TIMER_SCALE));
    ESP_ERROR_CHECK(timer_isr_register(s_timer_group, s_timer_idx, _process_loop_isr,
                                       nullptr, ESP_INTR_FLAG_LEVEL3, NULL));
    ESP_ERROR_CHECK(timer_enable_intr(s_timer_group, s_timer_idx));
    ESP_ERROR_CHECK(timer_start(s_timer_group, s_timer_idx));

    // Cool now create a task that will run our process loop.
    _go = true;
    xTaskCreate(_process_task, "process", 2 * 1024, NULL, 7, &_task_handle);
}

