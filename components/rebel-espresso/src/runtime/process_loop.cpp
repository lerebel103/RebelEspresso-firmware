#include "process_loop.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_intr_alloc.h>
#include <hal/timer_types.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>
#include <esp_event.h>
#include <driver/gptimer.h>
#include <esp_timer.h>
#include "events.h"
#include "process_image.h"
#include "hw_specs.h"

#define TAG "process"

#define TIMER_INTERVAL0_SEC (1.0)

static gptimer_handle_t s_timer;
static TaskHandle_t _process_task_handle = nullptr;
static bool _go = false;
static SemaphoreHandle_t s_semaphore = NULL;

/**
 * Timer interrupt handler that drives our process loop
 * @param para
 */
static bool IRAM_ATTR _process_loop_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata,
                                        void *user_ctx) {
  static BaseType_t xHigherPriorityTaskWoken;

  xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(s_semaphore, &xHigherPriorityTaskWoken);

  /* If xHigherPriorityTaskWoken was set to true you
  we should yield.  The actual macro used here is
  port specific. */
  if (xHigherPriorityTaskWoken != pdFALSE) {
    portYIELD_FROM_ISR();
  }

  return xHigherPriorityTaskWoken;
}

static void _process_task(void *) {
  ESP_LOGI(TAG, "Process loop starting");

  do {
    if (xSemaphoreTake(s_semaphore, portMAX_DELAY) == pdTRUE) {
      auto now_us = esp_timer_get_time();

      // Read latest temperatures from process image and dispatch to PID controllers
      for (uint8_t i = 0; i < PROCESS_IMAGE_MAX_SENSORS; i++) {
        measure_t data = process_image_read_temp(i);
        if (data.fault != 0 && data.value == 0 && now_us < 2000000) {
          // Skip initial faulted readings before sensor task has run
          continue;
        }
        hw_specs_handle_new_temp(now_us, data, i);
      }

      // Send down tick event (async) for Layer 4 consumers
      ESP_ERROR_CHECK(esp_event_post(MACHINE_EVENTS, TICK, (void *)&now_us, sizeof(uint64_t), portMAX_DELAY));

      auto elapsed_ms = (esp_timer_get_time() - now_us) / 1000;
      ESP_LOGI(TAG, "Free Heap: %lu, Min Heap: %lu, ctr loop: %lld ms", esp_get_free_heap_size(),
               esp_get_minimum_free_heap_size(), elapsed_ms);

      // Done, reset ISR to go again and maintain watchdog timer
      esp_task_wdt_reset();
    }
  } while (_go);
  ESP_LOGI(TAG, "Process loop ended");

  // Kill resources
  _process_task_handle = nullptr;
  vTaskDelete(nullptr);
}

void process_loop_init() {
  s_semaphore = xSemaphoreCreateBinary();

  gptimer_config_t timer_config = {.clk_src = GPTIMER_CLK_SRC_DEFAULT,
                                   .direction = GPTIMER_COUNT_UP,
                                   .resolution_hz = 1 * 1000 * 1000, // 1MHz, 1 tick = 1us
                                   .intr_priority = 3,
                                   .flags = {.intr_shared = 0, .allow_pd = false}};

  ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &s_timer));

  gptimer_alarm_config_t alarm_config = {
      .alarm_count = static_cast<int>((TIMER_INTERVAL0_SEC * 1000 * 1000)), // alarm target = 1s @resolution 1MHz
      .reload_count = 0,
      .flags =
          {
              .auto_reload_on_alarm = true,
          },
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(s_timer, &alarm_config));

  gptimer_event_callbacks_t cbs = {
      .on_alarm = _process_loop_isr, // register user callback
  };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer, &cbs, nullptr));
  ESP_ERROR_CHECK(gptimer_enable(s_timer));
  ESP_ERROR_CHECK(gptimer_start(s_timer));

  // Cool now create a task that will run our process loop.
  _go = true;
  esp_task_wdt_config_t cfg = {.timeout_ms = 2000, .idle_core_mask = 0, .trigger_panic = true};

  ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&cfg));
  xTaskCreate(_process_task, "process_loop", 3 * 1024, NULL, 7, &_process_task_handle);
  ESP_ERROR_CHECK(esp_task_wdt_add(_process_task_handle));
}
