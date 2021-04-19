#pragma once

#include <ctime>
#include <hw_config.h>
#include <esp_event_base.h>
#include <cJSON.h>

#include "hw/rtds.h"
#include "pid.h"
#include "rmt_duty_map.h"
#include "sys/str_utils.h"

#define BOILER_SSR_PIN GPIO_TRIG1_SSR

#define BOILER_CFG_JSON_KEY                 "boiler_temp."
#define NVS_BOILER_CFG_STORE                "cfg.boiler_temp"
#define NVS_BOILER_STATS_STORE              "sts.boiler_temp"

#define KEY_BOILER_MAINS_HZ                 "pid.mains_hz"
#define KEY_BOILER_TEMP_ERROR_RESTART_SEC   "pid.t_err_rest"

#define KEY_BOILER_STATS_OVER_TEMP          "t_over_limit"
#define KEY_BOILER_STATS_TEMP_ERROR         "t_read_error"
#define KEY_BOILER_STATS_TEMP_RANGE_ERROR   "t_range_error"



// Defined here so unit tests can find these
extern "C" const uint8_t BOILER_MAINS_HZ_DEFAULT;
extern "C" const uint16_t BOILER_TEMP_ERROR_RESTART_SEC_DEFAULT;


/**
 * Wrapper around boiler configuration
 */
struct boiler_temp_cfg_t {
    /**
     * Main PID settings
     */
    pid_cfg_t pid;

    /**
     * When non-zero, restart the MCU if we get successive errors for this long.
     */
    uint16_t temp_error_restart_time_sec;

    /**
     * What is the mains frequency, used to formulate variable time base pulses
     */
    uint8_t mains_hz;

    /**
     * Apply new configuration
     */
    void from_json(const cJSON *config) {
        pid.from_json(BOILER_CFG_JSON_KEY, config);

        cJSON *item = config->child;
        while (item) {
            if (strend(item->string, BOILER_CFG_JSON_KEY "mains_hz")) {
                mains_hz = item->valuedouble;
            } else if (strend(item->string, BOILER_CFG_JSON_KEY "temp_error_restart_time_sec")) {
                temp_error_restart_time_sec = item->valueint;
            }
            item = item->next;
        }
    }

    /**
     * Report current configuration
     */
    void to_json(cJSON *config, const char *base_key) {
        char *buf = (char *) malloc(64);

        sprintf(buf, "%s" BOILER_CFG_JSON_KEY, base_key);
        pid.to_json(config, buf);

        sprintf(buf, "%s" BOILER_CFG_JSON_KEY "mains_hz", base_key);
        cJSON_AddNumberToObject(config, buf, mains_hz);

        sprintf(buf, "%s" BOILER_CFG_JSON_KEY "temp_error_restart_time_sec", base_key);
        cJSON_AddNumberToObject(config, buf, temp_error_restart_time_sec);

        free(buf);
    }

};

struct boiler_temp_status_t {
    uint32_t temp_read_error_count;
    uint32_t temp_over_limit_count;
    uint32_t temp_out_of_range_count;

    /**
     * Report current configuration
     */
    void to_json(cJSON *config, const char *base_key) {
        char *buf = (char *) malloc(64);

        sprintf(buf, "%s" BOILER_CFG_JSON_KEY "temp_read_error_count", base_key);
        cJSON_AddNumberToObject(config, buf, temp_read_error_count);

        sprintf(buf, "%s" BOILER_CFG_JSON_KEY "temp_over_limit_count", base_key);
        cJSON_AddNumberToObject(config, buf, temp_over_limit_count);

        sprintf(buf, "%s" BOILER_CFG_JSON_KEY "temp_out_of_range_count", base_key);
        cJSON_AddNumberToObject(config, buf, temp_out_of_range_count);

        free(buf);
    }

};

void boiler_temp_init(esp_event_loop_handle_t event_loop);

void boiler_temp_delete();

void boiler_temp_process(uint64_t time_us, const rtd_data_t &data);

/**
 * Get current duty value applied to the SSR
 * @return
 */
int boiler_temp_get_duty();

/**
 * This is the effective setpoint, after trim has been applied to keep the brew head temp to target.
 */
double boiler_temp_get_trimmed_setpoint();


void boiler_set_active_setpoint(int idx);

/**
 * Increments the current setpoint by the specified value
 * @param inc floating point
 * @return New value, that is contained in min,max setpoint
 */
double boiler_setpoint_inc(double inc);

/**
 * Get the underlying configuration set.
 * @return Object representing the config of all boiler parameters.
 */
const struct boiler_temp_cfg_t &boiler_temp_get_cfg();

/**
 * Updates underlying config, partial keys are accepted.
 * @param json Config to be parsed.
 */
void boiler_temp_update_cfg(const cJSON *json);

/**
 * Whipes entire config with a new object
 * @param cfg
 */
void boiler_temp_set_cfg(boiler_temp_cfg_t cfg);

void boiler_temp_reset_cfg();

const boiler_temp_status_t &boiler_temp_get_status();

void boiler_temp_reset_stats();


