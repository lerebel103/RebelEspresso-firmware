#pragma once

#include <esp_event_base.h>
#include <cJSON.h>
#include <src/hw/rtds.h>
#include "pid.h"

#define BREW_TEC_CFG_JSON_KEY               "brew.tec."
#define NVS_BREW_TEC_CFG_STORE              "cfg.brew.tec"
#define NVS_BREW_TEC_STATS_STORE            "stats.brew.tec"

#define KEY_BREW_TEC_ENABLED                "enabled"
#define KEY_BREW_TEC_HYSTERESIS             "hysteresis"
#define KEY_BREW_TEC_MAX_TEMP               "max_tec_temp"

#define KEY_BREW_TEC_STATS_OVER_TEMP        "t_over_limit"
#define KEY_BREW_TEC_STATS_TEMP_ERROR       "t_read_error"
#define KEY_BREW_TEC_STATS_TEMP_RANGE_ERROR "t_range_error"

// Defined here so unit tests can find these
extern "C" const uint8_t BREW_TEC_ENABLED_DEFAULT;
extern "C" const double BREW_TEC_HYSTERESIS_DEFAULT;

/**
 * Wrapper around brew configuration
 */
struct brew_tec_cfg_t {
    /**
     * Main PID settings
     */
    pid_cfg_t pid;
    
    bool enabled;

    /**
     * Max temperature the TEC can take
     */
    double max_tec_temp;

    /**
     * Hysteresis band
     */
     double hysteresis;

    /**
     * Apply new configuration
     */
    void from_json(const cJSON *config) {
        pid.from_json(BREW_TEC_CFG_JSON_KEY, config);

        cJSON *item = config->child;
        while( item ) {
            if ( strend(item->string, BREW_TEC_CFG_JSON_KEY "enabled") ) {
                enabled = cJSON_IsTrue(item);
            } else if ( strend(item->string, BREW_TEC_CFG_JSON_KEY "hysteresis") ) {
                hysteresis = item->valuedouble;
            }
            item = item->next;
        }
    }

    /**
     * Report current configuration
     */
    void to_json(cJSON* config, const char* base_key) {
        char* buf = (char*) malloc(64);

        sprintf(buf, "%s" BREW_TEC_CFG_JSON_KEY, base_key);
        pid.to_json(config, buf);

        sprintf(buf, "%s" BREW_TEC_CFG_JSON_KEY "enabled", base_key);
        cJSON_AddBoolToObject(config, buf, enabled);

        sprintf(buf, "%s" BREW_TEC_CFG_JSON_KEY "hysteresis", base_key);
        cJSON_AddNumberToObject(config, buf, hysteresis);
        
        free(buf);
    }

};

struct brew_tec_status_t {
    uint32_t temp_read_error_count;
    uint32_t temp_over_limit_count;
    uint32_t temp_out_of_range_count;
    uint32_t tec_hot_side_error_count;
    uint32_t tec_cold_side_error_count;
    uint32_t tec_temp_delta_error_count;
    uint32_t tec_temp_hot_thres_error_count;
    uint32_t tec_temp_cold_thres_error_count;
    uint32_t tec_ic_error;

    /**
     * Report current configuration
     */
    void to_json(cJSON* config, const char* base_key) {
        char* buf = (char*) malloc(64);

        sprintf(buf, "%s" BREW_TEC_CFG_JSON_KEY "temp_read_error_count", base_key);
        cJSON_AddNumberToObject(config, buf, temp_read_error_count);

        sprintf(buf, "%s" BREW_TEC_CFG_JSON_KEY "temp_over_limit_count", base_key);
        cJSON_AddNumberToObject(config, buf, temp_over_limit_count);

        sprintf(buf, "%s" BREW_TEC_CFG_JSON_KEY "temp_out_of_range_count", base_key);
        cJSON_AddNumberToObject(config, buf, temp_out_of_range_count);

        sprintf(buf, "%s" BREW_TEC_CFG_JSON_KEY "tec_hot_side_error_count", base_key);
        cJSON_AddNumberToObject(config, buf, tec_hot_side_error_count);

        sprintf(buf, "%s" BREW_TEC_CFG_JSON_KEY "tec_cold_side_error_count", base_key);
        cJSON_AddNumberToObject(config, buf, tec_cold_side_error_count);

        sprintf(buf, "%s" BREW_TEC_CFG_JSON_KEY "tec_temp_delta_error_count", base_key);
        cJSON_AddNumberToObject(config, buf, tec_temp_delta_error_count);

        sprintf(buf, "%s" BREW_TEC_CFG_JSON_KEY "tec_ic_error", base_key);
        cJSON_AddNumberToObject(config, buf, tec_ic_error);

        free(buf);
    }

};

void brew_tec_init(esp_event_loop_handle_t event_loop);

void brew_tec_delete();

void brew_tec_process(uint64_t time_us, const rtd_data_t& data);

/**
 * Get current duty value applied to the SSR
 * @return
 */
int brew_tec_get_duty();
/**
 * Just got a new value for hot side of TEC
 * @param time_us
 * @param data
 */
void brew_tec_hot_updated(uint64_t time_us, const rtd_data_t& data);
/**
 * Just got a new value for cold side of TEC
 */
void brew_tec_cold_updated(uint64_t time_us, const rtd_data_t& data);

void brew_tec_set_active_setpoint(int idx);

/**
 * Increments the current setpoint by the specified value
 * @param inc floating point
 * @return New value, that is contained in min,max setpoint
 */
double brew_tec_setpoint_inc(double inc);

/**
 * Get the underlying configuration set.
 * @return Object representing the config of all brew parameters.
 */
const brew_tec_cfg_t &brew_tec_get_cfg();

/**
 * Updates underlying config, partial keys are accepted.
 * @param json Config to be parsed.
 */
void brew_tec_update_cfg(const cJSON* json);

/**
 * Whipes entire config with a new object
 * @param cfg
 */
void brew_tec_set_cfg(brew_tec_cfg_t cfg);

void brew_tec_reset_cfg();

const brew_tec_status_t& brew_tec_get_status();

void brew_tec_reset_stats();
