#pragma once

#include <ctime>
#include <hw_config.h>
#include <esp_event_base.h>
#include <cJSON.h>

#include "hw/rtds.h"
#include "pid.h"
#include "rmt_duty_map.h"
#include "str_utils.h"

#define BOILER_SSR_PIN GPIO_TRIG1_SSR

#define NVS_CFG_STORE "cfg.boiler"
#define KEY_BOILER_PID_P "pid.P"
#define KEY_BOILER_PID_I "pid.I"
#define KEY_BOILER_PID_D "pid.D"
#define KEY_BOILER_PID_I_RESET_SEC "pid.i_reset_sec"
#define KEY_BOILER_PID_I_RESET_TEMP "pid.i_reset_tem"
#define KEY_BOILER_PID_SETPOINT0 "pid.sp0"
#define KEY_BOILER_PID_SETPOINT1 "pid.sp1"
#define KEY_BOILER_PID_OVER_SETPOINT_PERC "pid.over_sp_per"
#define KEY_BOILER_PID_MIN_DUTY_BAND "pid.min_d_band"
#define KEY_BOILER_MAINS_HZ "pid.mains_hz"

#define NVS_STATS_STORE "stats.boiler"
#define KEY_BOILER_STATS_OVER_TEMP        "t_over_limit"
#define KEY_BOILER_STATS_TEMP_ERROR       "t_read_error"
#define KEY_BOILER_STATS_TEMP_RANGE_ERROR "t_range_error"


#define BOILER_SETPOINT0_MIN 50
#define BOILER_SETPOINT0_MAX 125
#define BOILER_SETPOINT1_MIN 110
#define BOILER_SETPOINT1_MAX 140

#define BOILER_CFG_JSON_KEY "boiler."

// Defined here so unit tests can find these
extern "C" const double BOILER_PID_P_DEFAULT;
extern "C" const double BOILER_PID_I_DEFAULT;
extern "C" const double BOILER_PID_D_DEFAULT;
extern "C" const int32_t BOILER_PID_I_RESET_SEC_DEFAULT;
extern "C" const double BOILER_PID_I_RESET_TEMP_DEFAULT;
extern "C" const double BOILER_PID_SETPOINT_DEFAULT;
extern "C" const double BOILER_PID_OVER_SETPOINT_PERC_DEFAULT;
extern "C" const uint8_t BOILER_MAINS_HZ_DEFAULT;


/**
 * Wrapper around boiler configuration
 */
struct boiler_temp_cfg_t {
    /**
     * Main PID settings
     */
    pid_cfg_t pid;

    /**
     * What is the mains frequency, used to formulate variable time base pulses
     */
    uint8_t mains_hz;

    /**
     * Apply new configuration
     */
    void from_json(const cJSON *config) {
        pid.from_json(config);

        cJSON *item = config->child;
        while( item ) {
            if ( strend(item->string, BOILER_CFG_JSON_KEY "mains_hz") ) {
                mains_hz = item->valuedouble;
            }
            item = item->next;
        }
    }

    /**
     * Report current configuration
     */
    void to_json(cJSON* config, const char* base_key) {
        char* buf = (char*) malloc(64);

        sprintf(buf, "%s" BOILER_CFG_JSON_KEY, base_key);
        pid.to_json(config, buf);

        sprintf(buf, "%s" BOILER_CFG_JSON_KEY "mains_hz", base_key);
        cJSON_AddNumberToObject(config, buf, mains_hz);

        free(buf);
    }

};

struct boiler_status_t {
    uint32_t temp_read_error_count;
    uint32_t temp_over_limit_count;
    uint32_t temp_out_of_range_count;

    /**
     * Report current configuration
     */
    void to_json(cJSON* config, const char* base_key) {
        char* buf = (char*) malloc(64);

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
const boiler_temp_cfg_t &boiler_temp_get_cfg();

/**
 * Updates underlying config, partial keys are accepted.
 * @param json Config to be parsed.
 */
void boiler_temp_update_cfg(const cJSON* json);

/**
 * Whipes entire config with a new object
 * @param cfg
 */
void boiler_temp_set_cfg(boiler_temp_cfg_t cfg);


void boiler_temp_reset_cfg();

const boiler_status_t& boiler_temp_get_status();

void boiler_temp_reset_stats();


