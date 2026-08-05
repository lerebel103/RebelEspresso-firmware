#include "sensor_task.h"
#include "process_image.h"
#include "rtds.h"
#include "hw_specs.h"
#include "boiler_refill.h"
#include "water_probe.h"

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

// Rolling window of probe voltages for the glitch-robust diagnostic median.
static water_probe_window_t s_probe_window;

// Debounced corrosion status tracker (advisory in M2).
static corrosion_monitor_t s_corrosion;

/**
 * Callback from rtds_update() — publishes each RTD reading into the process
 * image via the seqlock. The 1 Hz control loop reads these values later and
 * dispatches them to the PID handlers; this callback does not run the PID.
 */
static void _rtd_cb([[maybe_unused]] uint64_t time_us, const struct measure_t data, uint8_t idx) {
  if (idx < PROCESS_IMAGE_MAX_SENSORS) {
    // Publish via the seqlock so readers never see a torn (value, fault) pair.
    process_image_write_temp(idx, data);
  }
}

/**
 * Read the water level ADC and update the process image.
 *
 * IMPORTANT: The water level probe uses a voltage applied across electrodes
 * in the boiler water. Prolonged application causes galvanic corrosion
 * (electrolysis) that degrades the probe over time. The enable pin is toggled
 * on for the shortest possible duration (microseconds) and immediately
 * disabled after the ADC read completes. This function must NEVER be called
 * in standby or descale mode — the caller is responsible for gating on
 * power state and descale mode.
 */
static void _read_water_level(process_image_t *img) {
  uint8_t status = 0;
  double voltage = 0;

  // Enable probe voltage, read, disable (prevents electrolysis)
  gpio_set_level(PIN_WATER_LEVEL_ENABLE, WATER_LEVEL_SENSE_ON);
  hw_specs_read_water_level_mv(&status, &voltage);
  gpio_set_level(PIN_WATER_LEVEL_ENABLE, WATER_LEVEL_SENSE_OFF);

  img->water_level_mv = voltage;

  // Publish a glitch-robust median for diagnostics/corrosion monitoring.
  water_probe_window_push(&s_probe_window, (uint16_t)voltage);
  img->water_level_median_mv = water_probe_window_median(&s_probe_window);

  // Derive level OK from configured threshold.
  // Use the boiler refill config threshold for now.
  auto& refill_cfg = boiler_refill_get_cfg();
  img->water_level_ok = (status == 0) && (voltage <= refill_cfg.refill_mv_threshold);

  // Reset the debounced monitor whenever the thresholds change (e.g. after a
  // calibrate) so a fresh baseline clears any prior fault immediately instead of
  // holding it for up to corrosion_consistency_ms.
  static uint16_t s_last_warn_mv = 0;
  static uint16_t s_last_fault_mv = 0;
  if (refill_cfg.corrosion_warn_threshold_mv != s_last_warn_mv ||
      refill_cfg.corrosion_fault_threshold_mv != s_last_fault_mv) {
    corrosion_monitor_reset(&s_corrosion);
    img->corrosion_status = (uint8_t)CORROSION_OK;
    s_last_warn_mv = refill_cfg.corrosion_warn_threshold_mv;
    s_last_fault_mv = refill_cfg.corrosion_fault_threshold_mv;
  }

  // Corrosion status is only meaningful on a *wet* reading (an empty boiler
  // legitimately reads high). Evaluate the debounced status only when submerged
  // and monitoring is enabled; hold the last status otherwise.
  if (refill_cfg.corrosion_enabled && img->water_level_ok) {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    corrosion_status_t st =
        corrosion_monitor_update(&s_corrosion, img->water_level_median_mv, refill_cfg.corrosion_warn_threshold_mv,
                                 refill_cfg.corrosion_fault_threshold_mv, now_ms, refill_cfg.corrosion_consistency_ms);
    img->corrosion_status = (uint8_t)st;
  } else if (!refill_cfg.corrosion_enabled) {
    corrosion_monitor_reset(&s_corrosion);
    img->corrosion_status = (uint8_t)CORROSION_OK;
  }

  // Trust-aware level classification: an ADC fault is always untrusted; a corroded
  // probe is untrusted only while the corrosion guard is enabled. With the guard
  // off the machine ignores the corrosion thresholds for control (prior behaviour)
  // while still reporting voltage/status for tracking.
  bool submerged = (voltage <= refill_cfg.refill_mv_threshold);
  bool trusted = water_level_trusted(status == 0, (corrosion_status_t)img->corrosion_status,
                                     refill_cfg.corrosion_guard_enabled != 0);
  img->level_status = (uint8_t)water_level_classify(trusted, submerged);
}

static void _sensor_task(void *) {
  ESP_LOGI(TAG, "Sensor task started (interval=%dms)", SENSOR_SCAN_INTERVAL_MS);

  while (s_running) {
    auto *img = process_image_get();

    // Read all RTD temperature sensors (SPI via ADS124S08, ~5-10ms)
    // RTDs are passive resistance measurements — safe to read in any power state.
    auto now_us = esp_timer_get_time();
    rtds_update(_rtd_cb);

    // Read water level ADC — ONLY when machine is powered on and NOT in descale mode.
    // The water level probe works by applying a voltage across electrodes immersed
    // in the boiler water. This causes galvanic corrosion (electrolysis) over time.
    // To minimise probe degradation, the voltage is applied only momentarily during
    // each read and NEVER in standby or descale mode. The probe enable GPIO is
    // toggled on/off within _read_water_level() for the shortest possible pulse.
    if (img->power_on && !img->descale_mode) {
      _read_water_level(img);
    }

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

  water_probe_window_reset(&s_probe_window);
  corrosion_monitor_reset(&s_corrosion);

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
