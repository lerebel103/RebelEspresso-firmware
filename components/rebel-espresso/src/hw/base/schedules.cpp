#include "schedules.h"

#include "rtds.h"
#include <esp_log.h>
#include <src/events.h>
#include <esp_event.h>
#include <ctime>
#include "pid.h"
#include "brew_temp.h"
#include "power.h"

#define TAG "schedules"

#define NVS_KEY_SCHEDULES "schedules"

const uint8_t SCHEDULES_ENABLED_DEFAULT = 1;
const double SCHEDULES_DELTA_DEFAULT = 0.8;
const double SCHEDULES_HYSTERESIS_DEFAULT = 2.0;


static schedules_cfg_t s_cfg;

static uint64_t s_last_stats_save = 0;
static bool s_stats_changed = false;
static schedules_status_t s_stats;

static void _load_stats() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_SCHEDULES_STATS_STORE, NVS_READWRITE, &my_handle));


  nvs_close(my_handle);

  s_last_stats_save = 0;
  s_stats_changed = false;
}

static void _save_stats(uint64_t time) {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_SCHEDULES_STATS_STORE, NVS_READWRITE, &my_handle));

  nvs_close(my_handle);
  s_last_stats_save = time;
  s_stats_changed = false;
}

static void _load_nvram() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_SCHEDULES_CFG_STORE, NVS_READWRITE, &my_handle));

  size_t len = 2048;
  char *buffer = (char *) calloc(1, len);
  esp_err_t err = nvs_get_str(my_handle, NVS_KEY_SCHEDULES, buffer, &len);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Set default then
    strcpy(buffer, "{}");
    ESP_LOGI(TAG, "Key %s not found, setting to default %s", NVS_KEY_SCHEDULES, buffer);
    ESP_ERROR_CHECK(nvs_set_str(my_handle, NVS_KEY_SCHEDULES, buffer));
    ESP_ERROR_CHECK(nvs_commit(my_handle));
  }

  cJSON *root = cJSON_Parse(buffer);
  if (root != nullptr) {
    s_cfg.from_json(root);
    cJSON_Delete(root);
  } else {
    ESP_LOGE(TAG, "Could not load schedules, corrupt? %s", buffer);
  }
  free(buffer);

  nvs_close(my_handle);
}

static void _save_nvram() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_SCHEDULES_CFG_STORE, NVS_READWRITE, &my_handle));

  // Just write the whole object as JSON
  cJSON *root = cJSON_CreateObject();
  s_cfg.to_json(root, "");
  char *content = cJSON_PrintUnformatted(root);
  ESP_LOGI(TAG, "Saved schedules to nvram, size %d", strlen(content));
  ESP_ERROR_CHECK(nvs_set_str(my_handle, NVS_KEY_SCHEDULES, content));
  ESP_ERROR_CHECK(nvs_commit(my_handle));

  cJSON_free(content);
  cJSON_Delete(root);

  nvs_close(my_handle);
}

static void _tick_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
  if (!s_cfg.enabled) {
    return;
  }
  if (id != TICK) {
    return;
  }

  if (xEventGroupGetBits(status_event_group) & DESCALE_MODE_BIT) {
    ESP_LOGI(TAG, "Descaling, not running");
    return;
  }

  uint64_t now = 0;
  if (event_data != nullptr) {
    now = *(uint64_t *) event_data;
  }

  // See if we need to serialise stats, but pace it so we don't kill the flash
  if (s_stats_changed && (s_last_stats_save == 0 || (now - s_last_stats_save) >= (uint64_t) 5e6)) {
    _save_stats(now);
  }

  // Ok, check schedules if we have NTP and wifi going only for time sync
  if (xEventGroupGetBits(status_event_group) & SNTP_TIME_SYNCED_BIT) {
    time_t time_now;
    time(&time_now);
    struct tm *l_time = localtime(&time_now);

    // Pick up our schedule times and match with the desired day of the week, that's it.
    ESP_LOGD(TAG, "**************************** Today %d", l_time->tm_wday);

    auto times = s_cfg.times[l_time->tm_wday];
    for (int i = 0; i < DAILY_SCHEDULES_MAX; i++) {
      auto schedule = times[i];
      if (schedule.active) {
        if (schedule.start_hour == l_time->tm_hour && schedule.start_minute == l_time->tm_min) {
          // So we should be started, if not start!
          if (!power_is_active()) {
            ESP_LOGI(TAG, "Activating by schedule");
            power_active();
          }
        }
        if (schedule.stop_hour == l_time->tm_hour && schedule.stop_minute == l_time->tm_min) {
          // So we should be stopped, if not stop!
          if (power_is_active()) {
            ESP_LOGI(TAG, "Standby by schedule");
            power_standby();
          }
        }
      }
    }

  } else {
    ESP_LOGW(TAG, "No timesync, not running scheduler");
  }
}

void schedules_init() {
  s_cfg = {};

  _load_nvram();
  _load_stats();

  // Get our power events in place so we can run the process loop as needed
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, TICK,
                                             _tick_events, nullptr));
}

void schedules_delete() {
  ESP_ERROR_CHECK(esp_event_handler_unregister(MACHINE_EVENTS, TICK, _tick_events));
}

const schedules_cfg_t &schedules_get_cfg() {
  return s_cfg;
}

void schedules_set_cfg(schedules_cfg_t config) {
  // validate all fields
  s_cfg.enabled = config.enabled;
  for (int i = 0; i < MAX_DAYS; i++) {
    for (int j = 0; j < DAILY_SCHEDULES_MAX; j++) {
      s_cfg.times[i][j].active = config.times[i][j].active;
      if (config.times[i][j].start_hour >= 0 && config.times[i][j].start_hour < 24) {
        s_cfg.times[i][j].start_hour = config.times[i][j].start_hour;
      }
      if (config.times[i][j].start_minute >= 0 && config.times[i][j].start_minute < 60) {
        s_cfg.times[i][j].start_minute = config.times[i][j].start_minute;
      }
      if (config.times[i][j].stop_hour >= 0 && config.times[i][j].stop_hour < 24) {
        s_cfg.times[i][j].stop_hour = config.times[i][j].stop_hour;
      }
      if (config.times[i][j].stop_minute >= 0 && config.times[i][j].stop_minute < 60) {
        s_cfg.times[i][j].stop_minute = config.times[i][j].stop_minute;
      }
    }
  }

  // Save what we can then
  _save_nvram();
}

void schedules_update_cfg(const cJSON *json) {
  schedules_cfg_t new_config = s_cfg;
  new_config.from_json(json);
  schedules_set_cfg(new_config);
}


void schedules_reset_cfg() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_SCHEDULES_CFG_STORE, NVS_READWRITE, &my_handle));
  nvs_erase_all(my_handle);
  nvs_close(my_handle);

  _load_nvram();
}

const schedules_status_t &schedules_get_status() {
  return s_stats;
}

void schedules_reset_stats() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_SCHEDULES_STATS_STORE, NVS_READWRITE, &my_handle));
  nvs_erase_all(my_handle);
  nvs_close(my_handle);

  _load_stats();
}

