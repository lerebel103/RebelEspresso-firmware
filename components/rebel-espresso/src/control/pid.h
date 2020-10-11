#pragma once

#include <cstring>
#include "str_utils.h"

#define PID_CFG_JSON_KEY "pid."

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
    double setpoint;

    /**
     * How far above current setpoint we can go before we cut off the SSR and disable the PID
     */
    double over_setpoint_perc;

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
            } else if ( strend(item->string, PID_CFG_JSON_KEY "setpoint") ) {
                setpoint = item->valuedouble;
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
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "I_reset_sec", 32, base_key);
        cJSON_AddNumberToObject(config, buf, I_reset_sec);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "I_reset_temp", 32, base_key);
        cJSON_AddNumberToObject(config, buf, I_reset_temp);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "setpoint", 32, base_key);
        cJSON_AddNumberToObject(config, buf, setpoint);
        sprintf(buf, "%.*s" PID_CFG_JSON_KEY "over_setpoint_perc", 32, base_key);
        cJSON_AddNumberToObject(config, buf, over_setpoint_perc);
        free(buf);
    }
    

};