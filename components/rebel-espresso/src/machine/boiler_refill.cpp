#include <hal/gpio_types.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <hw_config.h>
#include <esp_log.h>
#include "boiler_refill.h"
#include "boiler_refill_states.h"
#include "out_signals.h"
#include "hw_specs.h"
#include <src/events.h>
#include <esp_event.h>
#include <src/utils/nvram_store.h>
#include <esp_timer.h>
#include <driver/gpio.h>

const static char *TAG = "refill";

#define BOILER_REFILL_NVS_CFG_STORE "cfg.b_refill"
#define MIN_SAMPLE_TIME_MS 250

const uint16_t BOILER_REFILL_START_DELAY_MS_DEFAULT = 1000;
const uint16_t BOILER_REFILL_STABILISE_MS_DEFAULT = 5;
const uint16_t BOILER_REFILL_ADC_NUM_READINGS_DEFAULT = 25;
const uint16_t BOILER_REFILL_REFILL_MV_THRESHOLD_DEFAULT = 2400;
const uint16_t BOILER_REFILL_MAX_REFILL_TIME_MS_DEFAULT = 15000;
const uint16_t BOILER_REFILL_LEVEL_LOW_HYSTERESIS_MS_DEFAULT = 750;
const uint16_t BOILER_REFILL_LEVEL_OK_HYSTERESIS_MS_DEFAULT = 1000;
const uint16_t BOILER_REFILL_CORROSION_ENABLED_DEFAULT = 0;       // inert until calibrated
const uint16_t BOILER_REFILL_CORROSION_GUARD_ENABLED_DEFAULT = 1; // fault gates heater/refill by default
const uint16_t BOILER_REFILL_CORROSION_BASELINE_MV_DEFAULT = 0;
const uint16_t BOILER_REFILL_CORROSION_WARN_MARGIN_MV_DEFAULT = 400;  // guessed, tune in field
const uint16_t BOILER_REFILL_CORROSION_FAULT_MARGIN_MV_DEFAULT = 800; // guessed, tune in field
const uint16_t BOILER_REFILL_CORROSION_WARN_THRESHOLD_MV_DEFAULT = 0;
const uint16_t BOILER_REFILL_CORROSION_FAULT_THRESHOLD_MV_DEFAULT = 0;
const uint16_t BOILER_REFILL_CORROSION_CONSISTENCY_MS_DEFAULT = 30000;

static boiler_refill_cfg_t s_cfg;
static boiler_refill_status_t s_status;

static double s_level_voltage = 0;
static uint8_t s_status_monitor = 0;

typedef void (*state_fn)(bool level_ok, TickType_t now_ms);

// This is the function pointer to the level check function (used by test mock)
static check_level_fn check_level = nullptr;

static void _load_nvram();

bool boiler_check_level() {
  // Enable voltage on probe - measured settle time is 70ns, which is bugger all,
  gpio_set_level(PIN_WATER_LEVEL_ENABLE, WATER_LEVEL_SENSE_ON);

  hw_specs_read_water_level_mv(&s_status_monitor, &s_level_voltage);

  // Done, disable to prevent electrolysis
  gpio_set_level(PIN_WATER_LEVEL_ENABLE, WATER_LEVEL_SENSE_OFF);
  return s_level_voltage <= s_cfg.refill_mv_threshold;
}

void boiler_set_check_level_fn(check_level_fn fn) {
  check_level = fn;
}

void boiler_refill_init() {
  check_level = boiler_check_level;
  _load_nvram();

  // Start with boiler not ok, until we can get a reading
  xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);

  // Output pins
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = ((1ULL << PIN_WATER_LEVEL_ENABLE));

  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  // Turn off outputs
  out_signals_set_level(OUT_SIGNALS_RELAY2, 0);
  gpio_set_level(PIN_WATER_LEVEL_ENABLE, WATER_LEVEL_SENSE_OFF);

  // Configure ADC input
  // Configure pins for voltage divider
  ESP_LOGI(TAG, "Initialising ADC pin input");

  // Note: refill state machine is now initialised and driven by the I/O scan task.

  ESP_LOGI(TAG, "Initialised");
}

void boiler_refill_delete() {
  // No event handlers to unregister — I/O scan owns the state machine now.
}

double boiler_refill_level_mv() {
  return s_level_voltage;
}

// -----------------------------------------------------------------------------------------
// Config stuff
// -----------------------------------------------------------------------------------------

static void _load_nvram() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(BOILER_REFILL_NVS_CFG_STORE, NVS_READWRITE, &my_handle));

  nvram_store_get_u16(my_handle, KEY_start_delay_ms, (uint16_t *)&s_cfg.start_delay_ms,
                      (void *)&BOILER_REFILL_START_DELAY_MS_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_stabilise_ms, (uint16_t *)&s_cfg.stabilise_ms,
                      (void *)&BOILER_REFILL_STABILISE_MS_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_refill_mv_threshold, (uint16_t *)&s_cfg.refill_mv_threshold,
                      (void *)&BOILER_REFILL_REFILL_MV_THRESHOLD_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_max_refill_time_ms, (uint16_t *)&s_cfg.max_refill_time_ms,
                      (void *)&BOILER_REFILL_MAX_REFILL_TIME_MS_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_adc_num_readings, (uint16_t *)&s_cfg.adc_num_readings,
                      (void *)&BOILER_REFILL_ADC_NUM_READINGS_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_level_low_hysteresis_ms, (uint16_t *)&s_cfg.level_low_hysteresis_ms,
                      (void *)&BOILER_REFILL_LEVEL_LOW_HYSTERESIS_MS_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_level_ok_hysteresis_ms, (uint16_t *)&s_cfg.level_ok_hysteresis_ms,
                      (void *)&BOILER_REFILL_LEVEL_OK_HYSTERESIS_MS_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_corrosion_enabled, (uint16_t *)&s_cfg.corrosion_enabled,
                      (void *)&BOILER_REFILL_CORROSION_ENABLED_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_corrosion_guard_enabled, (uint16_t *)&s_cfg.corrosion_guard_enabled,
                      (void *)&BOILER_REFILL_CORROSION_GUARD_ENABLED_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_corrosion_baseline_mv, (uint16_t *)&s_cfg.corrosion_baseline_mv,
                      (void *)&BOILER_REFILL_CORROSION_BASELINE_MV_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_corrosion_warn_margin_mv, (uint16_t *)&s_cfg.corrosion_warn_margin_mv,
                      (void *)&BOILER_REFILL_CORROSION_WARN_MARGIN_MV_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_corrosion_fault_margin_mv, (uint16_t *)&s_cfg.corrosion_fault_margin_mv,
                      (void *)&BOILER_REFILL_CORROSION_FAULT_MARGIN_MV_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_corrosion_warn_threshold_mv, (uint16_t *)&s_cfg.corrosion_warn_threshold_mv,
                      (void *)&BOILER_REFILL_CORROSION_WARN_THRESHOLD_MV_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_corrosion_fault_threshold_mv, (uint16_t *)&s_cfg.corrosion_fault_threshold_mv,
                      (void *)&BOILER_REFILL_CORROSION_FAULT_THRESHOLD_MV_DEFAULT);
  nvram_store_get_u16(my_handle, KEY_corrosion_consistency_ms, (uint16_t *)&s_cfg.corrosion_consistency_ms,
                      (void *)&BOILER_REFILL_CORROSION_CONSISTENCY_MS_DEFAULT);

  nvs_close(my_handle);
}

static void _save_nvram() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(BOILER_REFILL_NVS_CFG_STORE, NVS_READWRITE, &my_handle));

  nvram_store_set_u16(my_handle, KEY_start_delay_ms, &s_cfg.start_delay_ms);
  nvram_store_set_u16(my_handle, KEY_stabilise_ms, &s_cfg.stabilise_ms);
  nvram_store_set_u16(my_handle, KEY_adc_num_readings, &s_cfg.adc_num_readings);
  nvram_store_set_u16(my_handle, KEY_refill_mv_threshold, &s_cfg.refill_mv_threshold);
  nvram_store_set_u16(my_handle, KEY_max_refill_time_ms, &s_cfg.max_refill_time_ms);
  nvram_store_set_u16(my_handle, KEY_level_low_hysteresis_ms, &s_cfg.level_low_hysteresis_ms);
  nvram_store_set_u16(my_handle, KEY_level_ok_hysteresis_ms, &s_cfg.level_ok_hysteresis_ms);
  nvram_store_set_u16(my_handle, KEY_corrosion_enabled, &s_cfg.corrosion_enabled);
  nvram_store_set_u16(my_handle, KEY_corrosion_guard_enabled, &s_cfg.corrosion_guard_enabled);
  nvram_store_set_u16(my_handle, KEY_corrosion_baseline_mv, &s_cfg.corrosion_baseline_mv);
  nvram_store_set_u16(my_handle, KEY_corrosion_warn_margin_mv, &s_cfg.corrosion_warn_margin_mv);
  nvram_store_set_u16(my_handle, KEY_corrosion_fault_margin_mv, &s_cfg.corrosion_fault_margin_mv);
  nvram_store_set_u16(my_handle, KEY_corrosion_warn_threshold_mv, &s_cfg.corrosion_warn_threshold_mv);
  nvram_store_set_u16(my_handle, KEY_corrosion_fault_threshold_mv, &s_cfg.corrosion_fault_threshold_mv);
  nvram_store_set_u16(my_handle, KEY_corrosion_consistency_ms, &s_cfg.corrosion_consistency_ms);

  nvs_close(my_handle);
}

const boiler_refill_cfg_t& boiler_refill_get_cfg() {
  return s_cfg;
}

void boiler_refill_update_cfg(const cJSON *json) {
  boiler_refill_cfg_t new_config = s_cfg;
  new_config.from_json(json);
  boiler_refill_set_cfg(new_config);
}

void boiler_refill_set_cfg(boiler_refill_cfg_t config) {
  if (config.start_delay_ms < 10000) {
    s_cfg.start_delay_ms = config.start_delay_ms;
  } else {
    ESP_LOGE(TAG, "start_delay_ms is out of bounds: %d, ignoring.", config.start_delay_ms);
  }

  if (config.stabilise_ms < 150) {
    s_cfg.stabilise_ms = config.stabilise_ms;
  } else {
    ESP_LOGE(TAG, "stabilise_ms is out of bounds: %d, ignoring.", config.stabilise_ms);
  }

  if (config.adc_num_readings < 128) {
    s_cfg.adc_num_readings = config.adc_num_readings;
  } else {
    ESP_LOGE(TAG, "adc_num_readings is out of bounds: %d, ignoring.", config.adc_num_readings);
  }

  if (config.refill_mv_threshold >= 150 && config.refill_mv_threshold <= 3100) {
    s_cfg.refill_mv_threshold = config.refill_mv_threshold;
  } else {
    ESP_LOGE(TAG, "refill_mv_threshold is out of bounds: %d, ignoring.", config.refill_mv_threshold);
  }

  if (config.max_refill_time_ms >= 1000 && config.max_refill_time_ms < 30000) {
    s_cfg.max_refill_time_ms = config.max_refill_time_ms;
  } else {
    ESP_LOGE(TAG, "max_refill_time_ms is out of bounds: %d, ignoring.", config.max_refill_time_ms);
  }

  if (config.level_low_hysteresis_ms < 2000) {
    s_cfg.level_low_hysteresis_ms = config.level_low_hysteresis_ms;
  } else {
    ESP_LOGE(TAG, "level_low_hysteresis_ms is out of bounds: %d, ignoring.", config.level_low_hysteresis_ms);
  }

  if (config.level_ok_hysteresis_ms < 2000) {
    s_cfg.level_ok_hysteresis_ms = config.level_ok_hysteresis_ms;
  } else {
    ESP_LOGE(TAG, "level_ok_hysteresis_ms is out of bounds: %d, ignoring.", config.level_ok_hysteresis_ms);
  }

  // Corrosion monitoring (loosely validated — mV range 0..3100, margins editable).
  s_cfg.corrosion_enabled = config.corrosion_enabled ? 1 : 0;
  s_cfg.corrosion_guard_enabled = config.corrosion_guard_enabled ? 1 : 0;
  if (config.corrosion_baseline_mv <= 3100) {
    s_cfg.corrosion_baseline_mv = config.corrosion_baseline_mv;
  }
  if (config.corrosion_warn_margin_mv <= 3100) {
    s_cfg.corrosion_warn_margin_mv = config.corrosion_warn_margin_mv;
  }
  if (config.corrosion_fault_margin_mv <= 3100) {
    s_cfg.corrosion_fault_margin_mv = config.corrosion_fault_margin_mv;
  }
  if (config.corrosion_warn_threshold_mv <= 3100) {
    s_cfg.corrosion_warn_threshold_mv = config.corrosion_warn_threshold_mv;
  }
  if (config.corrosion_fault_threshold_mv <= 3100) {
    s_cfg.corrosion_fault_threshold_mv = config.corrosion_fault_threshold_mv;
  }
  if (config.corrosion_consistency_ms >= 1000) {
    s_cfg.corrosion_consistency_ms = config.corrosion_consistency_ms;
  }

  // Save what we can then
  _save_nvram();
}

void boiler_refill_reset_cfg() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(BOILER_REFILL_NVS_CFG_STORE, NVS_READWRITE, &my_handle));
  nvs_erase_all(my_handle);
  nvs_close(my_handle);

  _load_nvram();
}

const boiler_refill_status_t& boiler_refill_get_status() {
  return s_status;
}

void boiler_refill_reset_stats() {
  s_status.refill_error_count = 0;
}

void boiler_refill_calibrate_probe(uint16_t median_mv) {
  s_cfg.corrosion_baseline_mv = median_mv;

  uint32_t warn = (uint32_t)median_mv + s_cfg.corrosion_warn_margin_mv;
  uint32_t fault = (uint32_t)median_mv + s_cfg.corrosion_fault_margin_mv;
  s_cfg.corrosion_warn_threshold_mv = (uint16_t)(warn > 3100 ? 3100 : warn);
  s_cfg.corrosion_fault_threshold_mv = (uint16_t)(fault > 3100 ? 3100 : fault);
  s_cfg.corrosion_enabled = 1;

  ESP_LOGI(TAG, "Probe calibrated: baseline=%u warn=%u fault=%u mV", s_cfg.corrosion_baseline_mv,
           s_cfg.corrosion_warn_threshold_mv, s_cfg.corrosion_fault_threshold_mv);

  _save_nvram();
}
