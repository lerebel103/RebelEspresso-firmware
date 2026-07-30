#include <nvs.h>
#include <esp_system.h>
#include <esp_log.h>
#include <ctime>
#include <cstring>
#include <esp_event.h>
#include <esp_timer.h>
#include "app_metrics.h"
#include "common/identity.h"

#define TAG "app_metrics"
#define NVS_STATS_NAMESPACE "stats"

static device_metrics_t s_device_metrics = {};
static time_t _last_report_time = 0;

static void _record_metrics() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_STATS_NAMESPACE, NVS_READWRITE, &nvs_handle));
  nvs_get_u32(nvs_handle, "boot_count", &s_device_metrics.boot_count);
  nvs_get_u32(nvs_handle, "crash_count", &s_device_metrics.crash_count);
  nvs_get_u32(nvs_handle, "last_crash_reason", &s_device_metrics.last_crash_reason);

  auto reason = esp_reset_reason();
  if (reason != ESP_RST_DEEPSLEEP && reason != ESP_RST_POWERON && reason != ESP_RST_SW) {
    ESP_LOGE(TAG, "Detected crash with reset reason: %d", reason);
    s_device_metrics.crash_count++;
    s_device_metrics.last_crash_reason = reason;
  }

  s_device_metrics.boot_count++;

  nvs_set_u32(nvs_handle, "boot_count", s_device_metrics.boot_count);
  nvs_set_u32(nvs_handle, "crash_count", s_device_metrics.crash_count);
  nvs_set_u32(nvs_handle, "last_crash_reason", s_device_metrics.last_crash_reason);

  ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  nvs_close(nvs_handle);
}

void app_metrics_send(time_t now, char *buffer, size_t max_len) {
  // MQTT publishing removed — telemetry is no longer sent remotely.
  // Metrics are still recorded locally in NVS.
  _last_report_time = now;
}

bool app_metrics_update_required(int interval_sec) {
  return (time(nullptr) - _last_report_time) > (interval_sec);
}

void app_metrics_reset_update() {
  _last_report_time = 0;
}

device_metrics_t app_metrics_get() {
  return s_device_metrics;
}

void app_metrics_init() {
  _record_metrics();
  ESP_LOGI(TAG, " >>>>>>>>>>>>>>>>>> Boot count: %" PRIu32 "\n", s_device_metrics.boot_count);
}
