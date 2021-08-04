#include <cJSON.h>
#include <cstdint>
#include <malloc.h>
#include <esp_event_base.h>
#include "pid.h"

#define READY_INDICATOR_CFG_JSON_KEY            "ready_indicator."
#define NVS_READY_INDICATOR_CFG_STORE           "cfg.ready_ind"
#define NVS_READY_INDICATOR_STATS_STORE         "sts.ready_ind"

#define KEY_READY_INDICATOR_ENABLED             "enabled"
#define KEY_READY_INDICATOR_DELTA               "delta"
#define KEY_READY_INDICATOR_HYSTERESIS          "hysteresis"

// Defined here so unit tests can find these
extern "C" const double READY_INDICATOR_DELTA_DEFAULT;
extern "C" const double READY_INDICATOR_HYSTERESIS_DEFAULT;

/**
 * Wrapper around brew damper configuration
 */
struct ready_indicator_cfg_t {

    bool enabled;

    double delta;

    /**
     * Reset time in seconds, period during which the damper will not be applied when a brew was just completed
     */
    double hysteresis;

    /**
     * Apply new configuration
     */
    void from_json(const cJSON *config) {
        cJSON *item = config->child;
        while (item) {
            if (strend(item->string, READY_INDICATOR_CFG_JSON_KEY "enabled")) {
                enabled = cJSON_IsTrue(item);
            } else  if (strend(item->string, READY_INDICATOR_CFG_JSON_KEY "delta")) {
                delta = item->valuedouble;
            } else if (strend(item->string, READY_INDICATOR_CFG_JSON_KEY "hysteresis")) {
                hysteresis = item->valuedouble;
            }

            item = item->next;
        }
    }

    /**
     * Report current configuration
     */
    void to_json(cJSON *config, const char *base_key) {
        char *buf = (char *) malloc(64);

        sprintf(buf, "%s" READY_INDICATOR_CFG_JSON_KEY "enabled", base_key);
        cJSON_AddBoolToObject(config, buf, enabled);

        sprintf(buf, "%s" READY_INDICATOR_CFG_JSON_KEY "delta", base_key);
        cJSON_AddNumberToObject(config, buf, delta);

        sprintf(buf, "%s" READY_INDICATOR_CFG_JSON_KEY "hysteresis", base_key);
        cJSON_AddNumberToObject(config, buf, hysteresis);

        free(buf);
    }

};

struct ready_indicator_status_t {


    /**
     * Report current configuration
     */
    void to_json(cJSON *config, const char *base_key) {
        char *buf = (char *) malloc(64);

        free(buf);
    }

};

void ready_indicator_init(esp_event_loop_handle_t event_loop);

void ready_indicator_delete();

void ready_indicator_process(uint64_t time_us, const reading_t &brew_head_data);

/**
 * Get the underlying configuration set.
 * @return Object representing the config of all brew parameters.
 */
const ready_indicator_cfg_t &ready_indicator_get_cfg();

/**
 * Updates underlying config, partial keys are accepted.
 * @param json Config to be parsed.
 */
void ready_indicator_update_cfg(const cJSON *json);

/**
 * Whipes entire config with a new object
 * @param cfg
 */
void ready_indicator_set_cfg(ready_indicator_cfg_t cfg);

void ready_indicator_reset_cfg();

const ready_indicator_status_t &ready_indicator_get_status();

void ready_indicator_reset_stats();
