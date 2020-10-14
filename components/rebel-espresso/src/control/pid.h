#pragma once

#include <cstring>
#include "str_utils.h"

#define PID_CFG_JSON_KEY "pid."
#define MAX_SETPOINTS 2

struct pid_cfg_t {

    double P;

    double I;

    double D;

    /**
     * Integral time window is trimmed to this many seconds always
     */
    int I_reset_sec;

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
     * Minimum duty band, where any duty within this band is clamped to this value
     */
    double min_duty_band;

    /**
     * Parses the given JSON into this object
     */
    void from_json(const cJSON* config) {
        cJSON *item = config->child;
        while( item ) {
            if ( strend(item->string, PID_CFG_JSON_KEY "P") ) {
                P = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "I") ) {
                I = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "D") ) {
                D = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "I_reset_sec") ) {
                I_reset_sec = item->valueint;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "I_reset_temp") ) {
                I_reset_temp = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "setpoint0") ) {
                setpoints[0] = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "setpoint1") ) {
                setpoints[1] = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "over_setpoint_perc") ) {
                over_setpoint_perc = item->valuedouble;
            } else if ( strend(item->string, PID_CFG_JSON_KEY "min_duty_band") ) {
                min_duty_band = item->valuedouble;
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
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "I_reset_sec", 32, base_key);
        cJSON_AddNumberToObject(config, buf, I_reset_sec);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "I_reset_temp", 32, base_key);
        cJSON_AddNumberToObject(config, buf, I_reset_temp);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "setpoint0", 32, base_key);
        cJSON_AddNumberToObject(config, buf, setpoints[0]);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "setpoint1", 32, base_key);
        cJSON_AddNumberToObject(config, buf, setpoints[1]);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "over_setpoint_perc", 32, base_key);
        cJSON_AddNumberToObject(config, buf, over_setpoint_perc);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "min_duty_band", 32, base_key);
        cJSON_AddNumberToObject(config, buf, min_duty_band);
        free(buf);
    }
    

};