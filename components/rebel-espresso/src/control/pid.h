#pragma once

#include <cstring>
#include "str_utils.h"

#define PID_CFG_JSON_KEY "pid."

#define SETPOINT_MIN 50
#define SETPOINT_MAX 125

struct pid_cfg_t {

    double P = 3.5;

    double I = 0.5;

    double D = 35;

    /**
     * Integral time window is trimmed to this many seconds always
     */
    int I_reset_sec = 15;

    /**
     * Integral is discarded if delta temperature to setpoint is above this value
     */
    int I_reset_temp = 10;

    /**
     * Target setpoint
     */
    double setpoint = 0;

    /**
     * How far above current setpoint we can go before we cut off the SSR and disable the PID
     */
    double over_setpoint_perc = 8;

    /**
     * Parses the given JSON into this object
     */
    void from_json(const cJSON* config) {
        cJSON *item = config->child;
        while( item ) {
            if ( strend(item->string, PID_CFG_JSON_KEY "P") ) {
                auto val = item->valuedouble;
                if (val >= 0 && val < 20) {
                    P = val;
                }
            } else if ( strend(item->string, PID_CFG_JSON_KEY "I") ) {
                auto val = item->valuedouble;
                if (val >= 0 && val < 10) {
                    I = val;
                }
            } else if ( strend(item->string, PID_CFG_JSON_KEY "D") ) {
                auto val = item->valuedouble;
                if (val >= 0 && val < 300) {
                    D = val;
                }
            } else if ( strend(item->string, PID_CFG_JSON_KEY "I_reset_sec") ) {
                auto val = item->valueint;
                if (val >= 0 && val < 60) {
                    I_reset_sec = val;
                }
            } else if ( strend(item->string, PID_CFG_JSON_KEY "I_reset_temp") ) {
                auto val = item->valueint;
                if (val >= 0 && val < 40) {
                    I_reset_temp = val;
                }
            } else if ( strend(item->string, PID_CFG_JSON_KEY "setpoint") ) {
                auto val = item->valuedouble;
                if (val >= 0 && val <= 125) {
                    setpoint = val;
                }
            } else if ( strend(item->string, PID_CFG_JSON_KEY "over_setpoint_perc") ) {
                auto val = item->valuedouble;
                if (val >= 0 && val < 125) {
                    over_setpoint_perc = val;
                }
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