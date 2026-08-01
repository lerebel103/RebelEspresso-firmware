#include "sntp_sync.h"

#include "common/events_common.h"

#include <esp_attr.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <freertos/event_groups.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <sys/time.h>

#define TAG "sntp"

static EventGroupHandle_t xNetworkEventGroup;

#define MAX_TZ_LEN 64
char g_system_tz[MAX_TZ_LEN];

static TickType_t g_start_tick = 0;
static sntp_metrics_t s_metrics = {};

static void apply_tz(const char *timezone) {
  ESP_LOGI(TAG, "Setting POSIX timezone to %s", timezone);

  setenv("TZ", timezone, 1);
  tzset();
}

static void time_synced_handler(struct timeval *new_time) {
  xEventGroupSetBits(xNetworkEventGroup, SNTP_TIME_SYNCED_BIT);

  time_t now;
  time(&now);
  char strftime_buf[80];
  strftime(strftime_buf, sizeof(strftime_buf), "%c", localtime(&now));

  ESP_LOGI(TAG, "---> Got time sync, current time is %s", strftime_buf);

  if (s_metrics.sync_duration_ms == 0) {
    s_metrics.sync_duration_ms = xTaskGetTickCount() * portTICK_PERIOD_MS - g_start_tick;
  }
  s_metrics.last_sync_time = now;
}

static void initialize_sntp(const char *primary_server) {
  apply_tz(DEFAULT_SYSTEM_TZ);
  xEventGroupClearBits(xNetworkEventGroup, SNTP_TIME_SYNCED_BIT);

  g_start_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;
  esp_sntp_stop();

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  if (primary_server != nullptr) {
    ESP_LOGI(TAG, "Initializing SNTP with primary server '%s'", primary_server);
    esp_sntp_setservername(0, primary_server);
  } else {
    ESP_LOGI(TAG, "Initializing SNTP with primary server 'time.google.com'");
    esp_sntp_setservername(0, "time.google.com");
  }

  esp_sntp_setservername(1, "0.pool.ntp.org");
  esp_sntp_setservername(2, "1.pool.ntp.org");
  esp_sntp_setservername(3, "2.pool.ntp.org");

  sntp_set_time_sync_notification_cb(&time_synced_handler);

  esp_sntp_init();
}

void sntp_set_system_tz(const char *tz) {
  strcpy(g_system_tz, tz);
  apply_tz(tz);
}

const char *sntp_get_system_tz() {
  return g_system_tz;
}

void sntp_sync_init(EventGroupHandle_t networkEventGroup, const char *primary_server) {
  xNetworkEventGroup = networkEventGroup;
  apply_tz(sntp_get_system_tz());
  initialize_sntp(primary_server);
}

sntp_metrics_t sntp_sync_get_metrics() {
  return s_metrics;
}
