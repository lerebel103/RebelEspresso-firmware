#pragma once

#include <esp_event_base.h>
#include <cJSON.h>
#include "str_utils.h"

typedef void (*state_fn)(bool level_ok, TickType_t now_ms);
typedef bool (*check_level_fn)();

#define BOILER_REFILL_CFG_JSON_KEY ""

#define KEY_start_delay_ms "start_delay"
#define KEY_stabilise_ms "stabilise_ms"
#define KEY_adc_num_readings "adc_num_rdgs"
#define KEY_refill_mv_threshold "refill_v_thr"
#define KEY_max_refill_time_ms "max_r_time"
#define KEY_level_low_hysteresis_ms "level_low_ms"
#define KEY_level_ok_hysteresis_ms "level_ok_ms"
#define KEY_corrosion_enabled "cor_en"
#define KEY_corrosion_guard_enabled "cor_guard"
#define KEY_corrosion_baseline_mv "cor_base_mv"
#define KEY_corrosion_warn_margin_mv "cor_warn_m"
#define KEY_corrosion_fault_margin_mv "cor_flt_m"
#define KEY_corrosion_warn_threshold_mv "cor_warn_t"
#define KEY_corrosion_fault_threshold_mv "cor_flt_t"
#define KEY_corrosion_consistency_ms "cor_cons_ms"

extern "C" const uint16_t BOILER_REFILL_START_DELAY_MS_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_STABILISE_MS_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_ADC_NUM_READINGS_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_REFILL_MV_THRESHOLD_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_MAX_REFILL_TIME_MS_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_LEVEL_LOW_HYSTERESIS_MS_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_LEVEL_OK_HYSTERESIS_MS_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_CORROSION_ENABLED_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_CORROSION_GUARD_ENABLED_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_CORROSION_BASELINE_MV_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_CORROSION_WARN_MARGIN_MV_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_CORROSION_FAULT_MARGIN_MV_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_CORROSION_WARN_THRESHOLD_MV_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_CORROSION_FAULT_THRESHOLD_MV_DEFAULT;
extern "C" const uint16_t BOILER_REFILL_CORROSION_CONSISTENCY_MS_DEFAULT;

struct boiler_refill_cfg_t {
  /**
   * We get wrong readings on start, allow board to stabilise
   */
  uint16_t start_delay_ms;

  /**
   * Initial wait time before starting the check.
   * This is needed as the system is still initialising and we get a false reading
   */
  uint16_t stabilise_ms;

  /**
   * Readings are averaged, how many to take in succession.
   */
  uint16_t adc_num_readings;

  /**
   * Theshold over which we decice that the boiler is empty
   */
  uint16_t refill_mv_threshold;

  /**
   * Cap refill time and raise error if time is exceeded
   */
  uint16_t max_refill_time_ms;

  /**
   * Elapsed time where level was observed as low, after which we start refilling
   */
  uint16_t level_low_hysteresis_ms;

  /**
   * Elapsed time where level was observed as high, after which we stopped refilling
   */
  uint16_t level_ok_hysteresis_ms;

  // --- Probe corrosion monitoring (predictive maintenance) ---
  uint16_t corrosion_enabled;            ///< 0/1 — inert until calibrated
  uint16_t corrosion_guard_enabled;      ///< 0/1 — let a corrosion fault gate heater/refill
  uint16_t corrosion_baseline_mv;        ///< healthy wet reading captured at calibration
  uint16_t corrosion_warn_margin_mv;     ///< added to baseline -> warn threshold
  uint16_t corrosion_fault_margin_mv;    ///< added to baseline -> fault threshold
  uint16_t corrosion_warn_threshold_mv;  ///< derived at calibration, directly editable
  uint16_t corrosion_fault_threshold_mv; ///< derived at calibration, directly editable
  uint16_t corrosion_consistency_ms;     ///< sustained period before status changes

  /**
   * Apply new configuration
   */
  void from_json(const cJSON *config) {
    cJSON *item = config->child;
    while (item) {
      if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "start_delay_ms")) {
        start_delay_ms = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "stabilise_ms")) {
        stabilise_ms = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "adc_num_readings")) {
        adc_num_readings = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "refill_mv_threshold")) {
        refill_mv_threshold = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "max_refill_time_ms")) {
        max_refill_time_ms = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "level_low_hysteresis_ms")) {
        level_low_hysteresis_ms = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "level_ok_hysteresis_ms")) {
        level_ok_hysteresis_ms = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "corrosion_enabled")) {
        corrosion_enabled = cJSON_IsTrue(item) ? 1 : (uint16_t)item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "corrosion_guard_enabled")) {
        corrosion_guard_enabled = cJSON_IsTrue(item) ? 1 : (uint16_t)item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "corrosion_baseline_mv")) {
        corrosion_baseline_mv = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "corrosion_warn_margin_mv")) {
        corrosion_warn_margin_mv = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "corrosion_fault_margin_mv")) {
        corrosion_fault_margin_mv = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "corrosion_warn_threshold_mv")) {
        corrosion_warn_threshold_mv = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "corrosion_fault_threshold_mv")) {
        corrosion_fault_threshold_mv = item->valueint;
      } else if (strend(item->string, BOILER_REFILL_CFG_JSON_KEY "corrosion_consistency_ms")) {
        corrosion_consistency_ms = item->valueint;
      }
      item = item->next;
    }
  }

  /**
   * Report current configuration
   */
  void to_json(cJSON *config, const char *base_key) {
    char *buf = (char *)malloc(64);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "start_delay_ms", base_key);
    cJSON_AddNumberToObject(config, buf, start_delay_ms);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "stabilise_ms", base_key);
    cJSON_AddNumberToObject(config, buf, stabilise_ms);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "adc_num_readings", base_key);
    cJSON_AddNumberToObject(config, buf, adc_num_readings);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "refill_mv_threshold", base_key);
    cJSON_AddNumberToObject(config, buf, refill_mv_threshold);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "max_refill_time_ms", base_key);
    cJSON_AddNumberToObject(config, buf, max_refill_time_ms);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "level_low_hysteresis_ms", base_key);
    cJSON_AddNumberToObject(config, buf, level_low_hysteresis_ms);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "level_ok_hysteresis_ms", base_key);
    cJSON_AddNumberToObject(config, buf, level_ok_hysteresis_ms);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "corrosion_enabled", base_key);
    cJSON_AddBoolToObject(config, buf, corrosion_enabled != 0);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "corrosion_guard_enabled", base_key);
    cJSON_AddBoolToObject(config, buf, corrosion_guard_enabled != 0);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "corrosion_baseline_mv", base_key);
    cJSON_AddNumberToObject(config, buf, corrosion_baseline_mv);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "corrosion_warn_margin_mv", base_key);
    cJSON_AddNumberToObject(config, buf, corrosion_warn_margin_mv);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "corrosion_fault_margin_mv", base_key);
    cJSON_AddNumberToObject(config, buf, corrosion_fault_margin_mv);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "corrosion_warn_threshold_mv", base_key);
    cJSON_AddNumberToObject(config, buf, corrosion_warn_threshold_mv);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "corrosion_fault_threshold_mv", base_key);
    cJSON_AddNumberToObject(config, buf, corrosion_fault_threshold_mv);

    sprintf(buf, "%s" BOILER_REFILL_CFG_JSON_KEY "corrosion_consistency_ms", base_key);
    cJSON_AddNumberToObject(config, buf, corrosion_consistency_ms);

    free(buf);
  }
};

struct boiler_refill_status_t {
  uint16_t refill_error_count = 0;
};

void boiler_refill_init();

void boiler_refill_delete();

/**
 * Last reading, in millivolts
 */
double boiler_refill_level_mv();

/**
 * Used for testing, where we can inject a mocked function for level checking
 * @param fn
 */
void boiler_set_check_level_fn(check_level_fn fn);

/**
 * Get the underlying configuration set.
 * @return Object representing the config of all boiler parameters.
 */
const boiler_refill_cfg_t& boiler_refill_get_cfg();

/**
 * Updates underlying config, partial keys are accepted.
 * @param json Config to be parsed.
 */
void boiler_refill_update_cfg(const cJSON *json);

/**
 * Whipes entire config with a new object
 * @param cfg
 */
void boiler_refill_set_cfg(boiler_refill_cfg_t cfg);

void boiler_refill_reset_cfg();

/**
 * Capture the current (healthy, submerged) probe voltage as the corrosion
 * baseline, derive warn/fault thresholds from the configured margins, enable
 * monitoring, and persist to NVS. Called by the Calibrate action.
 */
void boiler_refill_calibrate_probe(uint16_t median_mv);

const boiler_refill_status_t& boiler_refill_get_status();

void boiler_refill_reset_stats();
