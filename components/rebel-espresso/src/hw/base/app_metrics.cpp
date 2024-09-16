#include <nvs.h>
#include <esp_system.h>
#include <esp_log.h>
#include <ctime>
#include <cstring>
#include <esp_event.h>
#include <esp_timer.h>
#include "app_metrics.h"
#include "shadow/shadow_handler.h"
#include "common/identity.h"
#include "wifi/wifi_connect.h"
#include "mqtt/mqtt_client.h"
#include "sntp/sntp_sync.h"
#include "common/events_common.h"
#include "fleet_provisioning/mqtt_provision.h"
#include "boiler_refill.h"
#include "boiler_temp.h"
#include "brew_temp.h"

#define TAG "app_metrics"
#define NVS_STATS_NAMESPACE "stats"
#define TOPIC_MAX_SIZE 128

static device_metrics_t s_device_metrics = {};
static time_t _last_report_time = 0;
static char metrics_topic[TOPIC_MAX_SIZE];

static void _record_metrics() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_STATS_NAMESPACE, NVS_READWRITE, &nvs_handle));
  nvs_get_u32(nvs_handle, "boot_count", &s_device_metrics.boot_count);
  nvs_get_u32(nvs_handle, "crash_count", &s_device_metrics.crash_count);
  nvs_get_u32(nvs_handle, "last_crash_reason", &s_device_metrics.last_crash_reason);

  auto reason = esp_reset_reason();
  if (reason != ESP_RST_DEEPSLEEP && reason != ESP_RST_POWERON && reason != ESP_RST_SW) {
    ESP_LOGE(TAG, "Detected crash with reset reason: %d", reason);
    // Then we have a crash
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
  static const char *metrics_format =
      R"({
      "metrics": {
        "timestamp": %)" PRIu64 R"(,
        "sys.uptime": %)" PRIu32 R"(,
        "sys.boot_cnt": %)" PRIu32 R"(,
        "sys.crash_cnt": %)" PRIu32 R"(,
        "sys.last_crash_reason": %)" PRIu32 R"(,
        "sys.heap_free": %)" PRIu32 R"(,
        "sys.heap_min": %)" PRIu32 R"(,
        "wifi.conn_attempt_cnt": %)" PRIu32 R"(,
        "wifi.disconn_cnt": %)" PRIu32 R"(,
        "wifi.conned_cnt": %)" PRIu32 R"(,
        "wifi.conn_duration_ms": %)" PRIu32 R"(,
        "wifi.rssi": %)" PRId8 R"(,
        "wifi.channel": %)" PRIu8 R"(,
        "wifi.ssid": "%s",
        "wifi.ap_bssid": "%s",
        "wifi.ip_addr": "%s",
        "wifi.gw_addr": "%s",
        "wifi.nm_addr": "%s",
        "sntp.last_sync_time": %)" PRIu64 R"(,
        "sntp.sync_duration_ms": %)" PRIu32 R"(,
        "mqtt.conn_attempt_cnt": %)" PRIu32 R"(,
        "mqtt.disconn_cnt": %)" PRIu32 R"(,
        "mqtt.conned_cnt": %)" PRIu32 R"(,
        "mqtt.conn_duration_ms": %)" PRIu32 R"(,
        "mqtt.tx_pkt_cnt": %)" PRIu32 R"(,
        "mqtt.tx_bytes_cnt": %)" PRIu64 R"(,
        "mqtt.rx_pkt_cnt": %)" PRIu32 R"(,
        "mqtt.rx_bytes_cnt": %)" PRIu64 R"(,
        "boiler_refill.err": %)" PRIu16 R"(,
        "boiler_temp.err_cnt": %)" PRIu32 R"(,
        "boiler_temp.over_lim_cnt": %)" PRIu32 R"(,
        "boiler_temp.range_cnt": %)" PRIu32 R"(,
        "brew_temp.err_cnt": %)" PRIu32 R"(,
        "brew_temp.over_lim_cnt": %)" PRIu32 R"(,
        "brew_temp.range_cnt": %)" PRIu32 R"(
        }
      })";

  auto wifi_metrics = wifi_connect_get_metrics();
  auto mqtt_metrics = mqtt_client_get_metrics();
  auto sntp_metrics = sntp_sync_get_metrics();
  const boiler_refill_status_t& boiler_refill = boiler_refill_get_status();
  const boiler_temp_status_t& boiler_temp = boiler_temp_get_status();
  const brew_temp_status_t& brew_temp = brew_temp_get_status();
  
  auto uptime = (uint32_t)(esp_timer_get_time() * 1e-6);
  size_t len = snprintf(buffer, max_len, metrics_format,
                        now, uptime,
                        s_device_metrics.boot_count, s_device_metrics.crash_count, s_device_metrics.last_crash_reason,
                        (uint32_t)esp_get_free_heap_size(),
                        (uint32_t)esp_get_minimum_free_heap_size(),
                        wifi_metrics.connect_attempt_count, wifi_metrics.disconnected_count,
                        wifi_metrics.connected_count, wifi_metrics.connect_duration_ms, wifi_metrics.rssi,
                        wifi_metrics.channel,
                        wifi_metrics.ssid, wifi_metrics.ap_bssid, wifi_metrics.ip_addr, wifi_metrics.gw_addr,
                        wifi_metrics.nm_addr,
                        (uint64_t) sntp_metrics.last_sync_time, sntp_metrics.sync_duration_ms,
                        mqtt_metrics.connect_attempt_count, mqtt_metrics.disconnected_count,
                        mqtt_metrics.connected_count,
                        mqtt_metrics.connect_duration_ms,
                        mqtt_metrics.tx_pkt_count, mqtt_metrics.tx_bytes_count,
                        mqtt_metrics.rx_pkt_count, mqtt_metrics.rx_bytes_count,
                        boiler_refill.refill_error_count,
                        boiler_temp.temp_read_error_count, boiler_temp.temp_over_limit_count, boiler_temp.temp_out_of_range_count,
                        brew_temp.brew_temp_read_error_count, brew_temp.brew_temp_over_limit_count, brew_temp.brew_temp_out_of_range_count
                        );

  _last_report_time = now;
  ESP_LOGD(TAG, "%.*s %d\n", len, buffer, len);

  MQTTPublishInfo_t publishInfo = {
      .qos = MQTTQoS_t::MQTTQoS1,
      .retain = false,
      .dup = false,
      .pTopicName = metrics_topic,
      .topicNameLength = (uint16_t) strlen(metrics_topic),
      .pPayload = buffer,
      .payloadLength = len,
  };

  // Send as best effort, not fussed
  mqtt_client_publish(&publishInfo, 0);

}

bool app_metrics_update_required(int interval_sec) {
  return (time(nullptr) - _last_report_time) > (interval_sec);
}

void app_metrics_reset_update() {
  _last_report_time = 0;
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_CONNECTED_EVENT && !mqtt_provisioning_active()) {
    // Refresh metrics on new connection
    _last_report_time = 0;
  }
}

device_metrics_t app_metrics_get() {
  return s_device_metrics;
}


void app_metrics_init() {
  _record_metrics();

  // Regular telemetry
  sprintf(metrics_topic, "%s/%s/telemetry/metrics", CMAKE_THING_TYPE, identity_thing_id());
  // Register connect events so we can send shadow on connect
  ESP_ERROR_CHECK(esp_event_handler_register(CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, nullptr));

  ESP_LOGI(TAG, " >>>>>>>>>>>>>>>>>> Boot count: %" PRIu32 "\n", s_device_metrics.boot_count);
}