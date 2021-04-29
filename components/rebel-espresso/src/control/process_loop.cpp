#include "process_loop.h"
#include "boiler_temp.h"
#include "brew_tec.h"
#include "pump.h"

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
#include <hw/rtds.h>
#include <hw/r1.0/hw_config.h>
#include <esp_event.h>
#include "events.h"
#include "brew_temp.h"
#include "ready_indicator.h"

#define TAG "process"
#define TIMER_DIVIDER         16  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds

#define TIMER_INTERVAL0_SEC   ( 1.0 )

static timer_idx_t s_timer_idx = TIMER_0;
static timer_group_t s_timer_group = TIMER_GROUP_0;
static TaskHandle_t _process_task_handle = nullptr;
static bool _go = false;
static SemaphoreHandle_t s_semaphore = NULL;
static esp_event_loop_handle_t s_event_loop;

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

    timer_group_enable_alarm_in_isr(s_timer_group, s_timer_idx);
}

/**
 * Realtime handler for new temps
 */
static void _handle_new_temp(uint64_t time_us, const rtd_data_t &data, uint8_t idx) {
    switch (idx) {
        case RTD_BOILER_IDX:
            boiler_temp_process(time_us, data);
            break;
        case RTD_BREW_HEAD_IDX:
            brew_tec_process(time_us, data);
            brew_temp_process(time_us, data);
            ready_indicator_process(time_us, data);
            break;
        case RTD_TEC_HOT_IDX:
            brew_tec_hot_updated(time_us, data);
            break;
        case RTD_TEC_COLD_IDX:
            brew_tec_cold_updated(time_us, data);
            break;
    }
}


static void _process_task(void *) {
    ESP_LOGI(TAG, "Process loop starting");

    do {
        if (xSemaphoreTake(s_semaphore, portMAX_DELAY) == pdTRUE) {
            // Do it
            ESP_LOGI(TAG, "Process, heap: %d, min: %d", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());

            // Get latest temperatures
            rtds_update(_handle_new_temp);

            // Done, reset ISR to go again and maintain watchdog timer
            esp_task_wdt_reset();
        }
    } while (_go);
    ESP_LOGI(TAG, "Process loop ended");

    // Kill resources
    _process_task_handle = nullptr;
    vTaskDelete(nullptr);
}


static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        //ESP_LOGI(TAG, "Stopping process loop");

        //esp_task_wdt_delete(_process_task_handle);
        //vTaskSuspend(_process_task_handle);
        //ESP_ERROR_CHECK(timer_pause(s_timer_group, s_timer_idx));
    } else if (id == POWER_ACTIVE) {
        //ESP_LOGI(TAG, "Activating process loop");

        // We want a strict watchdog timer on this one
        //ESP_ERROR_CHECK(timer_start(s_timer_group, s_timer_idx));
        //vTaskResume(_process_task_handle);
        //ESP_ERROR_CHECK(esp_task_wdt_add(_process_task_handle));
    }
}


void process_loop_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
    s_semaphore = xSemaphoreCreateBinary();

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
    ESP_ERROR_CHECK(timer_pause(s_timer_group, s_timer_idx));


    // Cool now create a task that will run our process loop.
    _go = true;
    //ESP_ERROR_CHECK( esp_task_wdt_init(10, true));
    xTaskCreate(_process_task, "process_loop", 3 * 1024, NULL, 10, &_process_task_handle);
    //ESP_ERROR_CHECK(esp_task_wdt_add(_process_task_handle));
    //vTaskSuspend(_process_task_handle);

    // Get our power events in place so we can run the process loop as needed
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));

}

