#include <cJSON.h>
#include <cstdint>
#include <malloc.h>
#include <sys/param.h>
#include <esp_event_base.h>
#include "pid.h"

#define SCHEDULES_CFG_JSON_KEY            ""
#define NVS_SCHEDULES_CFG_STORE           "cfg.sched"
#define NVS_SCHEDULES_STATS_STORE         "sts.sched"


// Defined here so unit tests can find these
extern "C" const double SCHEDULES_DELTA_DEFAULT;
extern "C" const double SCHEDULES_HYSTERESIS_DEFAULT;

#define MAX_DAYS 7
#define DAILY_SCHEDULES_MAX 1

struct daily_schedule_t {
  bool active;
  int start_hour;
  int start_minute;
  int stop_hour;
  int stop_minute;
};


/**
 * Wrapper around a bunch of daily schedules
 */
struct schedules_cfg_t {

  bool enabled;

  daily_schedule_t times[MAX_DAYS][DAILY_SCHEDULES_MAX];

  void parse_time(cJSON *item, int *hour, int *minutes) {
    const char *str = cJSON_GetStringValue(item);
    auto minutes_str = strstr(str, ":");
    if (minutes_str != nullptr) {
      minutes_str = minutes_str + 1;
      *hour = atoi(str);
      *minutes = atoi(minutes_str);
    }
  }

  void parse_time_schedule(int i, const cJSON *elm) {
    if (!cJSON_IsArray(elm)) {
      return;
    }

    // Clear first
    for (int j = 0; j < DAILY_SCHEDULES_MAX; j++) {
      times[i][j] = {};
    }

    int num = MIN(cJSON_GetArraySize(elm), DAILY_SCHEDULES_MAX);
    for (int j = 0; j < num; j++) {
      cJSON *item = cJSON_GetArrayItem(elm, j);
      if (cJSON_HasObjectItem(item, "en") &&
          cJSON_HasObjectItem(item, "start") &&
          cJSON_HasObjectItem(item, "stop")) {

        times[i][j].active = cJSON_IsTrue(cJSON_GetObjectItem(item, "en"));
        parse_time(cJSON_GetObjectItem(item, "start"), &times[i][j].start_hour, &times[i][j].start_minute);
        parse_time(cJSON_GetObjectItem(item, "stop"), &times[i][j].stop_hour, &times[i][j].stop_minute);
      }
    }
  }

  /**
   * Apply new configuration
   */
  void from_json(const cJSON *config) {
    cJSON *item = config->child;
    while (item) {
      if (strend(item->string, SCHEDULES_CFG_JSON_KEY "en")) {
        enabled = cJSON_IsTrue(item);
      } else if (strend(item->string, SCHEDULES_CFG_JSON_KEY "mon")) {
        parse_time_schedule(1, item);
      } else if (strend(item->string, SCHEDULES_CFG_JSON_KEY "tue")) {
        parse_time_schedule(2, item);
      } else if (strend(item->string, SCHEDULES_CFG_JSON_KEY "wed")) {
        parse_time_schedule(3, item);
      } else if (strend(item->string, SCHEDULES_CFG_JSON_KEY "thu")) {
        parse_time_schedule(4, item);
      } else if (strend(item->string, SCHEDULES_CFG_JSON_KEY "fri")) {
        parse_time_schedule(5, item);
      } else if (strend(item->string, SCHEDULES_CFG_JSON_KEY "sat")) {
        parse_time_schedule(6, item);
      } else if (strend(item->string, SCHEDULES_CFG_JSON_KEY "sun")) {
        parse_time_schedule(0, item);
      }

      item = item->next;
    }
  }

  /**
   * Report current configuration
   */
  void to_json(cJSON *config, const char *base_key) {
    char *buf = (char *) malloc(64);

    sprintf(buf, "%s" SCHEDULES_CFG_JSON_KEY "en", base_key);
    cJSON_AddBoolToObject(config, buf, enabled);

    static const char *days[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

    for (int i = 0; i < MAX_DAYS; i++) {
      sprintf(buf, "%s" SCHEDULES_CFG_JSON_KEY "%s", base_key, days[i]);
      cJSON *array = cJSON_AddArrayToObject(config, buf);
      for (int j = 0; j < DAILY_SCHEDULES_MAX; j++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddBoolToObject(item, "en", times[i][j].active);
        sprintf(buf, "%02d:%02d", times[i][j].start_hour, times[i][j].start_minute);
        cJSON_AddStringToObject(item, "start", buf);
        sprintf(buf, "%02d:%02d", times[i][j].stop_hour, times[i][j].stop_minute);
        cJSON_AddStringToObject(item, "stop", buf);
        cJSON_AddItemToArray(array, item);
      }
    }

    free(buf);
  }

};

struct schedules_status_t {

  /**
   * Report current configuration
   */
  void to_json(cJSON *config, const char *base_key) {
    char *buf = (char *) malloc(64);

    free(buf);
  }

};

void schedules_init();

void schedules_handle_cfg(char* buffer, size_t len);

void schedules_delete();

/**
 * Get the underlying configuration set.
 * @return Object representing the config of all brew parameters.
 */
const schedules_cfg_t &schedules_get_cfg();

/**
 * Updates underlying config, partial keys are accepted.
 * @param json Config to be parsed.
 */
void schedules_update_cfg(const cJSON *json);

/**
 * Whipes entire config with a new object
 * @param cfg
 */
void schedules_set_cfg(schedules_cfg_t cfg);

void schedules_reset_cfg();

const schedules_status_t &schedules_get_status();

void schedules_reset_stats();
