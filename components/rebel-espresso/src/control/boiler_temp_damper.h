#include <cJSON.h>
#include <cstdint>
#include <malloc.h>
#include "pid.h"

#define BOILER_DAMPER_CFG_JSON_KEY            "boiler_damper."
#define NVS_BOILER_DAMPER_CFG_STORE           "cfg.boiler.dpr"
#define NVS_BOILER_DAMPER_STATS_STORE         "sts.boiler.dpr"

#define KEY_BOILER_DAMPER_ENABLED             "enabled"
#define KEY_BOILER_DAMPER_PERC                "damping_perc"
#define KEY_BOILER_DAMPER_RESET_SEC           "reset_sec"

#define KEY_BOILER_DAMPER_STATS_OVER_TEMP           "t_over_limit"
#define KEY_BOILER_DAMPER_STATS_TEMP_ERROR          "t_read_error"
#define KEY_BOILER_DAMPER_STATS_TEMP_RANGE_ERROR    "t_range_error"

// Defined here so unit tests can find these
extern "C" const double BOILER_DAMPER_PERC_DEFAULT;
extern "C" const double BOILER_DAMPER_RESET_SEC_DEFAULT;

/**
 * Wrapper around brew damper configuration
 */
struct boiler_temp_damper_cfg_t {
    /**
     * Main PID settings
     */
    pid_cfg_t pid;

    bool enabled;

    /**
     * Max damping that can be applied in % of setpoint
     */
    double max_damping_perc;

    /**
     * Reset time in seconds, period during which the damper will not be applied when a brew was just completed
     */
    double reset_time_sec;

    /**
     * Apply new configuration
     */
    void from_json(const cJSON *config) {
        pid.from_json(BOILER_DAMPER_CFG_JSON_KEY, config);

        cJSON *item = config->child;
        while (item) {
            if (strend(item->string, BOILER_DAMPER_CFG_JSON_KEY "enabled")) {
                enabled = cJSON_IsTrue(item);
            } else  if (strend(item->string, BOILER_DAMPER_CFG_JSON_KEY "max_damping_perc")) {
                max_damping_perc = item->valuedouble;
            } else if (strend(item->string, BOILER_DAMPER_CFG_JSON_KEY "reset_time_sec")) {
                reset_time_sec = item->valuedouble;
            }

            item = item->next;
        }
    }

    /**
     * Report current configuration
     */
    void to_json(cJSON *config, const char *base_key) {
        char *buf = (char *) malloc(64);

        sprintf(buf, "%s" BOILER_DAMPER_CFG_JSON_KEY, base_key);
        pid.to_json(config, buf);

        sprintf(buf, "%s" BOILER_DAMPER_CFG_JSON_KEY "enabled", base_key);
        cJSON_AddBoolToObject(config, buf, enabled);

        sprintf(buf, "%s" BOILER_DAMPER_CFG_JSON_KEY "max_damping_perc", base_key);
        cJSON_AddNumberToObject(config, buf, max_damping_perc);

        sprintf(buf, "%s" BOILER_DAMPER_CFG_JSON_KEY "reset_time_sec", base_key);
        cJSON_AddNumberToObject(config, buf, reset_time_sec);

        free(buf);
    }

};

struct boiler_temp_damper_status_t {

    uint32_t brew_temp_read_error_count;
    uint32_t brew_temp_over_limit_count;
    uint32_t brew_temp_out_of_range_count;

    /**
     * Report current configuration
     */
    void to_json(cJSON *config, const char *base_key) {
        char *buf = (char *) malloc(64);

        sprintf(buf, "%s" BOILER_DAMPER_CFG_JSON_KEY "brew_temp_read_error_count", base_key);
        cJSON_AddNumberToObject(config, buf, brew_temp_read_error_count);

        sprintf(buf, "%s" BOILER_DAMPER_CFG_JSON_KEY "brew_temp_over_limit_count", base_key);
        cJSON_AddNumberToObject(config, buf, brew_temp_over_limit_count);

        sprintf(buf, "%s" BOILER_DAMPER_CFG_JSON_KEY "brew_temp_out_of_range_count", base_key);
        cJSON_AddNumberToObject(config, buf, brew_temp_out_of_range_count);

        free(buf);
    }

};

void boiler_temp_damper_init(esp_event_loop_handle_t event_loop);

void boiler_temp_damper_delete();

void boiler_temp_damper_process(uint64_t time_us, const rtd_data_t &brew_head_data);

/**
 * Get current duty value applied to the SSR
 * @return
 */
int boiler_temp_damper_get_duty();

double boiler_temp_damper_adjust_setpoint(double setpoint);


/**
 * Get the underlying configuration set.
 * @return Object representing the config of all brew parameters.
 */
const boiler_temp_damper_cfg_t &boiler_temp_damper_get_cfg();

/**
 * Updates underlying config, partial keys are accepted.
 * @param json Config to be parsed.
 */
void boiler_temp_damper_update_cfg(const cJSON *json);

/**
 * Whipes entire config with a new object
 * @param cfg
 */
void boiler_temp_damper_set_cfg(boiler_temp_damper_cfg_t cfg);

void boiler_temp_damper_reset_cfg();

const boiler_temp_damper_status_t &boiler_temp_damper_get_status();

void boiler_temp_damper_reset_stats();
