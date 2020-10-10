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

#define BOILER_CFG_JSON_KEY "boiler."

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
    uint8_t mains_hz = MAINS_50HZ;

    /**
     * Apply new configuration
     */
    void from_json(const cJSON *config) {
        pid.from_json(config);

        cJSON *item = config->child;
        while( item ) {
            if ( strend(item->string, BOILER_CFG_JSON_KEY "mains_hz") ) {
                auto val = item->valuedouble;
                if (val == 50) {
                    mains_hz = MAINS_50HZ;
                } else if (val == 60) {
                    mains_hz = MAINS_60HZ;
                }
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

void boiler_temp_init(esp_event_loop_handle_t event_loop);

void boiler_temp_delete();

void boiler_temp_process(uint64_t time_us, const rtd_data_t &data);

/**
 * Get the underlying configuration set.
 * @return Object representing the config of all boiler parameters.
 */
const boiler_temp_cfg_t &boiler_temp_get_cfg();

void boiler_temp_cfg_set(boiler_temp_cfg_t &cfg);


