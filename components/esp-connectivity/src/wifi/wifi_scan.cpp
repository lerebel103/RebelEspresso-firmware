#include "wifi_scan.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <algorithm>

#include "wifi_manager.h"

#define TAG "wifi_scan"

static const char *_auth_mode_str(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN:
      return "open";
    case WIFI_AUTH_WEP:
      return "wep";
    case WIFI_AUTH_WPA_PSK:
      return "wpa";
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
    case WIFI_AUTH_WPA2_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "wpa2";
    case WIFI_AUTH_WPA3_PSK:
      return "wpa3";
    default:
      return "wpa2";
  }
}

wifi_scan_list_t wifi_scan_perform() {
  wifi_scan_list_t list = {};

  wifi_scan_config_t scan_config = {};
  scan_config.show_hidden = false;
  scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scan_config.scan_time.active.min = 100;
  scan_config.scan_time.active.max = 300;

  // Try scanning directly first — works when STA is connected (run state).
  // Only disconnect if scan fails because STA is actively connecting.
  bool did_disconnect = false;
  esp_err_t err = esp_wifi_scan_start(&scan_config, true);

  if (err == ESP_ERR_WIFI_STATE) {
    // STA is in connecting state — must disconnect temporarily
    ESP_LOGI(TAG, "STA connecting, disconnecting temporarily for scan");
    wifi_manager_suppress_reconnect(true);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    did_disconnect = true;
    err = esp_wifi_scan_start(&scan_config, true);
  }

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
    if (did_disconnect) {
      wifi_manager_suppress_reconnect(false);
      esp_wifi_connect();
    }
    return list;
  }

  uint16_t ap_count = 0;
  esp_wifi_scan_get_ap_num(&ap_count);

  if (ap_count == 0) {
    ESP_LOGI(TAG, "No networks found");
    esp_wifi_scan_get_ap_records(&ap_count, nullptr); // Clear scan results
    return list;
  }

  // Cap to reasonable limit
  uint16_t fetch_count = ap_count > 64 ? 64 : ap_count;
  wifi_ap_record_t *records = (wifi_ap_record_t *)calloc(fetch_count, sizeof(wifi_ap_record_t));
  if (records == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate scan result buffer");
    esp_wifi_scan_get_ap_records(&fetch_count, nullptr);
    return list;
  }

  esp_wifi_scan_get_ap_records(&fetch_count, records);

  // Deduplicate by SSID (keep strongest signal) and build result list
  for (int i = 0; i < fetch_count && list.count < WIFI_SCAN_MAX_RESULTS; i++) {
    // Skip empty SSIDs
    if (strlen((char *)records[i].ssid) == 0) {
      continue;
    }

    // Check if we already have this SSID
    bool duplicate = false;
    for (int j = 0; j < list.count; j++) {
      if (strcmp(list.results[j].ssid, (char *)records[i].ssid) == 0) {
        // Keep the one with stronger signal
        if (records[i].rssi > list.results[j].rssi) {
          list.results[j].rssi = records[i].rssi;
          strncpy(list.results[j].auth, _auth_mode_str(records[i].authmode), sizeof(list.results[j].auth) - 1);
          list.results[j].channel = records[i].primary;
        }
        duplicate = true;
        break;
      }
    }

    if (!duplicate) {
      wifi_scan_result_t *result = &list.results[list.count];
      strncpy(result->ssid, (char *)records[i].ssid, sizeof(result->ssid) - 1);
      result->rssi = records[i].rssi;
      strncpy(result->auth, _auth_mode_str(records[i].authmode), sizeof(result->auth) - 1);
      result->channel = records[i].primary;
      list.count++;
    }
  }

  free(records);

  // Sort by RSSI descending (strongest first)
  std::sort(list.results, list.results + list.count,
            [](const wifi_scan_result_t& a, const wifi_scan_result_t& b) { return a.rssi > b.rssi; });

  ESP_LOGI(TAG, "Scan complete: %d unique networks found", list.count);

  // Reconnect STA if we disconnected it for the scan
  if (did_disconnect) {
    wifi_manager_suppress_reconnect(false);
    esp_wifi_connect();
  }

  return list;
}
