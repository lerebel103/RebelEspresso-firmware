#include "sensor_task.h"
#include "process_image.h"
#include "rtds.h"
#include "hw_specs.h"
#include "boiler_refill.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <hw_config.h>
#include <driver/gpio.h>

#define TAG "sensor_task"

// Sleep between full sensor scan passes (milliseconds).
#define SENSOR_SCAN_INTERVAL_MS 150

static TaskHandle_t s_task_handle = nullptr;
static bool s_running = false;

/**
 * Callback from rtds_update() — writes each RTD reading to the process image
 * and dispatches to existing PID handlers.
 */
static void _rtd_cb(uint64_t time_us, const struct measure_t data, uint8_t idx) {
  auto *img = process_image_get();
  if (idx < PROCESS_IMAGE_MAX_SENSORS) {
    img->temperatures[idx] = data;
  }

  // Dispatch to existing control logic (boiler_temp_process, brew_temp_process)
  hw_specs_handle_new_temp(time_us, data, idx);
}

/**
 * Read the water level ADC and update the process image.
 */
static void _read_water_level(process_image_t *img) {
  uint8_t status = 0;
  double voltage = 0;

  // Enable probe voltage, read, disable (prevents electrolysis)
  gpio_set_level(PIN_WATER_LEVEL_ENABLE, WATER_LEVEL_SENSE_ON);
  hw_specs_read_water_level_mv(&status, &voltage);
  gpio_set_level(PIN_WATER_LEVEL_ENABLE, WATER_LEVEL_SENSE_OFF);

  img->water_level_mv = voltage;

  // Derive level OK from configured threshold.
  // Use the boiler refill config threshold for now.
  auto& refill_cfg = boiler_refill_get_cfg();
  img->water_level_ok = (status == 0) && (voltage <= refill_cfg.refill_mv_threshold);
}

static void _sensor_task(void *) {
  ESP_LOGI(TAG, "Sensor task started (interval=%dms)", SENSOR_SCAN_INTERVAL_MS);

  while (s_running) {
    auto *img = process_image_get();

    // Read all RTD temperature sensors (SPI via ADS124S08, ~5-10ms)
    auto now_us = esp_timer_get_time();
    rtds_update(_rtd_cb);

    // Read water level ADC (also SPI via ADS124S08)
    _read_water_level(img);

    auto elapsed_ms = (esp_timer_get_time() - now_us) / 1000;
    ESP_LOGD(TAG, "Sensor scan: %lld ms", elapsed_ms);

    // Sleep until next pass
    vTaskDelay(pdMS_TO_TICKS(SENSOR_SCAN_INTERVAL_MS));
  }

  ESP_LOGI(TAG, "Sensor task stopped");
  s_task_handle = nullptr;
  vTaskDelete(nullptr);
}

extern "C" void sensor_task_init(void) {
  if (s_task_handle != nullptr) {
    ESP_LOGW(TAG, "Already initialised");
    return;
  }

  // Note: PIN_WATER_LEVEL_ENABLE GPIO is configured by boiler_refill_init().
  // When legacy refill code is removed, pin configuration moves here.

  s_running = true;
  xTaskCreate(_sensor_task, "sensor_task", 3072, nullptr, 6, &s_task_handle);

  ESP_LOGI(TAG, "Initialised");
}

extern "C" void sensor_task_delete(void) {
  if (s_task_handle == nullptr) {
    return;
  }

  s_running = false;
  while (s_task_handle != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(SENSOR_SCAN_INTERVAL_MS * 2));
  }

  ESP_LOGI(TAG, "Deleted");
}
