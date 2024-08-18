#pragma once

#include <cstring>
#include <cJSON.h>
#include "str_utils.h"
#include "src/sys/nvram_store.h"
#include "window.h"


#define PID_CFG_JSON_KEY "pid."
#define MAX_SETPOINTS 2

extern "C" const double PID_P_DEFAULT;
extern "C" const double PID_I_DEFAULT;
extern "C" const double PID_D_DEFAULT;
extern "C" const double PID_I_RESET_TEMP_DEFAULT;
extern "C" const double PID_SETPOINT0_DEFAULT;
extern "C" const double PID_SETPOINT1_DEFAULT;
extern "C" const double PID_OVER_SETPOINT_PERC_DEFAULT;

#define SETPOINT0_MIN 50
#define SETPOINT0_MAX 140
#define SETPOINT1_MIN 80
#define SETPOINT1_MAX 140

struct pid_cfg_t {

    double P;

    double I;

    double D;

    /**
     * Integral is discarded if delta temperature to setpoint is above this value
     */
    double I_reset_temp;

    /**
     * Target setpoint
     */
    double setpoints[MAX_SETPOINTS];

    int active_setpoint = 0;

    /**
     * How far above current setpoint we can go before we cut off the SSR and disable the PID
     */
    double over_setpoint_perc;


    /**
     * Parses the given JSON into this object
     */
    void from_json(const char* prefix, const cJSON* config) {
        cJSON *item = config->child;
        while( item ) {
            // Check if this key is for us
            if (strstr(item->string, prefix) == NULL) {
                item = item->next;
                continue;
            }

            if ( strend(item->string, PID_CFG_JSON_KEY "P") ) {
                P = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "I") ) {
                I = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "D") ) {
                D = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "I_reset_temp") ) {
                I_reset_temp = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "setpoint0") ) {
                setpoints[0] = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "setpoint1") ) {
                setpoints[1] = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "over_setpoint_perc") ) {
                over_setpoint_perc = item->valuedouble;
            }

            item = item->next;
        }
    }

    void to_json(cJSON* config, const char* base_key) {
        char* buf = (char*)malloc(64);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "P", 32, base_key);
        cJSON_AddNumberToObject(config, buf, P);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "I", 32, base_key);
        cJSON_AddNumberToObject(config, buf, I);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "D", 32, base_key);
        cJSON_AddNumberToObject(config, buf, D);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "I_reset_temp", 32, base_key);
        cJSON_AddNumberToObject(config, buf, I_reset_temp);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "setpoint0", 32, base_key);
        cJSON_AddNumberToObject(config, buf, setpoints[0]);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "setpoint1", 32, base_key);
        cJSON_AddNumberToObject(config, buf, setpoints[1]);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "over_setpoint_perc", 32, base_key);
        cJSON_AddNumberToObject(config, buf, over_setpoint_perc);
        free(buf);
    }
};

struct pid_struct_t {
    double error = 0;
    double proportional = 0;
    double integral = 0;
    double derivative = 0;

    uint64_t last_time_us = 0;
    double last_data_value = 0;
};

struct pid_result_t {
    double duty;
    int is_over_threshold;
};

void pid_init(pid_struct_t& pid);

void pid_reset(pid_struct_t& pid);

pid_result_t pid_process(
        pid_struct_t& pid,
        pid_cfg_t& cfg,
        uint64_t time_us, const measure_t& data);

void pid_load_nvram(nvs_handle my_handle, pid_cfg_t& cfg);

void pid_save_nvram(nvs_handle my_handle, pid_cfg_t& cfg);

void pid_save_setpoint(nvs_handle my_handle, pid_cfg_t& cfg);

void pid_update(pid_cfg_t& dest, const pid_cfg_t& src);
