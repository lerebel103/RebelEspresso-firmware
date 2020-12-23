#include <cJSON.h>
#include <cstdint>
#include <malloc.h>
#include "pid.h"

#define BREW_TEMP_CFG_JSON_KEY            "brew_temp."
#define NVS_BREW_TEMP_CFG_STORE           "cfg.brew_temp"
#define NVS_BREW_TEMP_STATS_STORE         "sts.brew_temp"

#define KEY_BREW_TEMP_ENABLED             "enabled"
#define KEY_BREW_TEMP_PERC                "damping_perc"
#define KEY_BREW_TEMP_RESET_SEC           "reset_sec"

#define KEY_BREW_TEMP_STATS_OVER_TEMP           "t_over_limit"
#define KEY_BREW_TEMP_STATS_TEMP_ERROR          "t_read_error"
#define KEY_BREW_TEMP_STATS_TEMP_RANGE_ERROR    "t_range_error"

// Defined here so unit tests can find these
extern "C" const double BREW_TEMP_PERC_DEFAULT;
extern "C" const double BREW_TEMP_RESET_SEC_DEFAULT;

/**
 * Wrapper around brew damper configuration
 */
struct brew_temp_cfg_t {
    /**
     * Main PID settings
     */
    pid_cfg_t pid;

    bool enabled;

    /**
     * Max damping that can be applied in % of setpoint of boiler
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
        pid.from_json(BREW_TEMP_CFG_JSON_KEY, config);

        cJSON *item = config->child;
        while (item) {
            if (strend(item->string, BREW_TEMP_CFG_JSON_KEY "enabled")) {
                enabled = cJSON_IsTrue(item);
            } else  if (strend(item->string, BREW_TEMP_CFG_JSON_KEY "max_damping_perc")) {
                max_damping_perc = item->valuedouble;
            } else if (strend(item->string, BREW_TEMP_CFG_JSON_KEY "reset_time_sec")) {
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

        sprintf(buf, "%s" BREW_TEMP_CFG_JSON_KEY, base_key);
        pid.to_json(config, buf);

        sprintf(buf, "%s" BREW_TEMP_CFG_JSON_KEY "enabled", base_key);
        cJSON_AddBoolToObject(config, buf, enabled);

        sprintf(buf, "%s" BREW_TEMP_CFG_JSON_KEY "max_damping_perc", base_key);
        cJSON_AddNumberToObject(config, buf, max_damping_perc);

        sprintf(buf, "%s" BREW_TEMP_CFG_JSON_KEY "reset_time_sec", base_key);
        cJSON_AddNumberToObject(config, buf, reset_time_sec);

        free(buf);
    }

};

struct brew_temp_status_t {

    uint32_t brew_temp_read_error_count;
    uint32_t brew_temp_over_limit_count;
    uint32_t brew_temp_out_of_range_count;

    /**
     * Report current configuration
     */
    void to_json(cJSON *config, const char *base_key) {
        char *buf = (char *) malloc(64);

        sprintf(buf, "%s" BREW_TEMP_CFG_JSON_KEY "brew_temp_read_error_count", base_key);
        cJSON_AddNumberToObject(config, buf, brew_temp_read_error_count);

        sprintf(buf, "%s" BREW_TEMP_CFG_JSON_KEY "brew_temp_over_limit_count", base_key);
        cJSON_AddNumberToObject(config, buf, brew_temp_over_limit_count);

        sprintf(buf, "%s" BREW_TEMP_CFG_JSON_KEY "brew_temp_out_of_range_count", base_key);
        cJSON_AddNumberToObject(config, buf, brew_temp_out_of_range_count);

        free(buf);
    }

};

void brew_temp_init(esp_event_loop_handle_t event_loop);

void brew_temp_delete();

void brew_temp_process(uint64_t time_us, const rtd_data_t &brew_head_data);

/**
 * Get current duty value applied to the SSR
 * @return
 */
int brew_temp_get_duty();

/**
 * Takes the current boiler setpoint and dampens it
 * @param setpoint
 * @return
 */
double brew_temp_dampen_boiler_setpoint(double setpoint);

double brew_temp_get_setpoint();

/**
 * Get the underlying configuration set.
 * @return Object representing the config of all brew parameters.
 */
const brew_temp_cfg_t &brew_temp_get_cfg();

/**
 * Updates underlying config, partial keys are accepted.
 * @param json Config to be parsed.
 */
void brew_temp_update_cfg(const cJSON *json);

/**
 * Whipes entire config with a new object
 * @param cfg
 */
void brew_temp_set_cfg(brew_temp_cfg_t cfg);

void brew_temp_reset_cfg();

const brew_temp_status_t &brew_temp_get_status();

void brew_temp_reset_stats();
