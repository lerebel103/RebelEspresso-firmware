#include "wifi_manager.h"

#include "wifi_ap.h"
#include "wifi_scan.h"
#include "common/events_common.h"
#include "sntp/sntp_sync.h"

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <freertos/event_groups.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#define TAG "wifi_mgr"

// Internal event bits for signaling connection results
#define CONNECT_SUCCESS_BIT BIT0
#define CONNECT_FAIL_BIT BIT1

static EventGroupHandle_t xNetworkEventGroup;
static EventGroupHandle_t s_connect_event_group;
static wifi_manager_mode_t s_mode = WIFI_MGR_MODE_IDLE;
static wifi_metrics_t s_metrics = {};
static int64_t _start_time;
static bool s_is_connecting = false;
static bool s_sta_started = false;
static esp_netif_t *s_sta_netif = nullptr;

static TimerHandle_t s_ap_fallback_timer = nullptr;
static bool s_initial_connect = true;
static bool s_suppress_reconnect = false;

/**
 * Check if WiFi credentials are stored by reading the STA config from the
 * WiFi driver. This is the same storage that esp_wifi_set_config() writes to
 * and that the old BLE provisioning (network_prov_mgr) used.
 *
 * Must be called AFTER esp_wifi_init().
 */
static bool _has_stored_wifi_config(wifi_config_t *out_cfg) {
  wifi_config_t cfg = {};
  esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to read WiFi config: %s", esp_err_to_name(err));
    return false;
  }

  // Check if SSID is non-empty
  if (strlen((char *)cfg.sta.ssid) == 0) {
    return false;
  }

  if (out_cfg) {
    memcpy(out_cfg, &cfg, sizeof(wifi_config_t));
  }
  return true;
}

/**
 * Store WiFi credentials via esp_wifi_set_config(). This writes to the same
 * NVS location the WiFi driver uses internally, ensuring compatibility with
 * previously provisioned devices.
 */
static bool _store_credentials(const char *ssid, const char *password) {
  wifi_config_t wifi_cfg = {};
  strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
  if (password && strlen(password) > 0) {
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
  }

  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to store WiFi config: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "WiFi credentials stored for SSID: %s", ssid);
  return true;
}

/**
 * AP fallback timer callback — schedules AP activation.
 * We don't call wifi_ap_start directly from the timer callback because
 * timer callbacks have limited stack and restricted context.
 */
static void _ap_fallback_task(void *arg) {
  ESP_LOGW(TAG, "STA connection timeout — activating Soft-AP fallback");
  s_mode = WIFI_MGR_MODE_AP;
  wifi_ap_start(xNetworkEventGroup);
  vTaskDelete(NULL);
}

static void _ap_fallback_timer_cb(TimerHandle_t timer) {
  // Spawn a task to do the actual AP activation (needs more stack than timer context provides)
  xTaskCreate(_ap_fallback_task, "ap_start", 3072, NULL, 5, NULL);
}

static void _start_ap_fallback_timer(uint32_t timeout_ms) {
  if (s_ap_fallback_timer == nullptr) {
    s_ap_fallback_timer =
        xTimerCreate("ap_fallback", pdMS_TO_TICKS(timeout_ms), pdFALSE, nullptr, _ap_fallback_timer_cb);
  } else {
    xTimerChangePeriod(s_ap_fallback_timer, pdMS_TO_TICKS(timeout_ms), 0);
  }
  xTimerStart(s_ap_fallback_timer, 0);
}

static void _stop_ap_fallback_timer() {
  if (s_ap_fallback_timer != nullptr) {
    xTimerStop(s_ap_fallback_timer, 0);
  }
}

static bool _is_ap_fallback_timer_running() {
  if (s_ap_fallback_timer == nullptr) {
    return false;
  }
  return xTimerIsTimerActive(s_ap_fallback_timer) != pdFALSE;
}

/**
 * WiFi/IP event handler.
 */
static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    s_sta_started = true;
    s_metrics.connect_attempt_count++;
    _start_time = esp_timer_get_time();
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    _stop_ap_fallback_timer();

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    sprintf(s_metrics.ip_addr, IPSTR, IP2STR(&event->ip_info.ip));
    sprintf(s_metrics.gw_addr, IPSTR, IP2STR(&event->ip_info.gw));
    sprintf(s_metrics.nm_addr, IPSTR, IP2STR(&event->ip_info.netmask));
    ESP_LOGI(TAG, "Connected with IP %s, GW %s, NM %s", s_metrics.ip_addr, s_metrics.gw_addr, s_metrics.nm_addr);

    // Set hostname
    char hostname[128];
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(hostname, sizeof(hostname), "rebel-%02x%02x%02x", mac[3], mac[4], mac[5]);
    esp_netif_set_hostname(event->esp_netif, hostname);
    ESP_LOGI(TAG, "Hostname set to %s", hostname);

    // If AP was running, schedule shutdown
    if (wifi_ap_is_active()) {
      ESP_LOGI(TAG, "STA connected while AP active — scheduling AP shutdown");
      // Delay AP shutdown to allow HTTP response to reach client
      vTaskDelay(pdMS_TO_TICKS(CONFIG_WIFI_AP_SHUTDOWN_DELAY_MS));
      wifi_ap_stop(xNetworkEventGroup);
    }

    s_mode = WIFI_MGR_MODE_STA;
    s_initial_connect = false;

    xEventGroupSetBits(xNetworkEventGroup, WIFI_CONNECTED_BIT);

    // Signal connect event group for wifi_manager_connect() callers
    if (s_connect_event_group) {
      xEventGroupSetBits(s_connect_event_group, CONNECT_SUCCESS_BIT);
    }

    // Start SNTP
    sntp_sync_init(xNetworkEventGroup);
    s_metrics.connected_count++;
    s_metrics.connect_duration_ms = (esp_timer_get_time() - _start_time) / 1000;
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "WiFi disconnected");
    xEventGroupClearBits(xNetworkEventGroup, WIFI_CONNECTED_BIT);
    s_metrics.disconnected_count++;

    if (s_is_connecting) {
      // This was an explicit connect attempt that failed
      if (s_connect_event_group) {
        xEventGroupSetBits(s_connect_event_group, CONNECT_FAIL_BIT);
      }
    } else if (s_suppress_reconnect) {
      // Disconnect was intentional (e.g., for WiFi scan) — don't reconnect
      ESP_LOGI(TAG, "Reconnection suppressed (scan in progress)");
    } else {
      // Unexpected disconnect during normal operation — try reconnecting
      ESP_LOGI(TAG, "Attempting reconnection...");
      _start_time = esp_timer_get_time();
      esp_wifi_connect();

      // Start the AP fallback timer ONCE (don't restart on each retry).
      // The timer fires after 30s of continuous failure.
      if (!wifi_ap_is_active() && !_is_ap_fallback_timer_running()) {
        ESP_LOGI(TAG, "Starting AP fallback timer (%d ms)", CONFIG_WIFI_STA_RECONNECT_TIMEOUT_MS);
        _start_ap_fallback_timer(CONFIG_WIFI_STA_RECONNECT_TIMEOUT_MS);
      }
    }
  }
}

void wifi_manager_init(EventGroupHandle_t networkEventGroup) {
  xNetworkEventGroup = networkEventGroup;
  s_connect_event_group = xEventGroupCreate();

  // Initialize TCP/IP
  ESP_ERROR_CHECK(esp_netif_init());

  // Register event handlers
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &_event_handler, NULL));

  // Create default STA netif
  s_sta_netif = esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Check for stored credentials using the WiFi driver's own config storage.
  // This reads from the same NVS location that esp_wifi_set_config() writes to,
  // which is where the old BLE provisioning (network_prov_mgr) stored credentials.
  wifi_config_t stored_cfg = {};
  bool has_creds = _has_stored_wifi_config(&stored_cfg);

  if (has_creds) {
    ESP_LOGI(TAG, "Found stored credentials for SSID: %s — attempting STA connection", (char *)stored_cfg.sta.ssid);

    // Start STA with the stored config (already set in the driver from NVS)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start AP fallback timer (15s for initial boot)
    _start_ap_fallback_timer(CONFIG_WIFI_STA_CONNECT_TIMEOUT_MS);
  } else {
    ESP_LOGW(TAG, "No stored WiFi credentials — starting Soft-AP directly");
    s_mode = WIFI_MGR_MODE_AP;

    // Need to start WiFi in STA mode first for scanning to work from AP mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_ap_start(xNetworkEventGroup);
  }
}

wifi_manager_mode_t wifi_manager_get_mode() {
  return s_mode;
}

wifi_metrics_t wifi_manager_get_metrics() {
  if (s_mode == WIFI_MGR_MODE_STA || s_mode == WIFI_MGR_MODE_AP_STA) {
    wifi_ap_record_t record;
    if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) {
      snprintf(s_metrics.ssid, sizeof(s_metrics.ssid), "%s", (char *)record.ssid);
      snprintf(s_metrics.ap_bssid, sizeof(s_metrics.ap_bssid), "%02hx:%02hx:%02hx:%02hx:%02hx:%02hx", record.bssid[0],
               record.bssid[1], record.bssid[2], record.bssid[3], record.bssid[4], record.bssid[5]);
      s_metrics.channel = record.primary;
      s_metrics.rssi = record.rssi;
    }
  }

  return s_metrics;
}

bool wifi_manager_connect(const char *ssid, const char *password, uint32_t timeout_ms) {
  ESP_LOGI(TAG, "Attempting connection to SSID: %s", ssid);

  // Stop AP fallback timer if running
  _stop_ap_fallback_timer();

  // Clear connection event bits
  xEventGroupClearBits(s_connect_event_group, CONNECT_SUCCESS_BIT | CONNECT_FAIL_BIT);

  // Disconnect from current network if connected
  s_is_connecting = true;
  esp_wifi_disconnect();

  // Ensure we're in a mode that supports STA
  wifi_mode_t current_mode;
  esp_wifi_get_mode(&current_mode);
  if (current_mode == WIFI_MODE_AP) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  } else if (current_mode != WIFI_MODE_APSTA) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  }

  // Store credentials via esp_wifi_set_config (persists to NVS automatically)
  if (!_store_credentials(ssid, password ? password : "")) {
    s_is_connecting = false;
    return false;
  }

  // If STA hasn't been started yet, start it; otherwise just reconnect
  if (!s_sta_started) {
    ESP_ERROR_CHECK(esp_wifi_start());
  } else {
    esp_wifi_connect();
  }

  // Wait for connection result
  EventBits_t bits = xEventGroupWaitBits(s_connect_event_group, CONNECT_SUCCESS_BIT | CONNECT_FAIL_BIT, pdTRUE, pdFALSE,
                                         pdMS_TO_TICKS(timeout_ms));

  s_is_connecting = false;

  if (bits & CONNECT_SUCCESS_BIT) {
    ESP_LOGI(TAG, "Successfully connected to %s", ssid);
    return true;
  }

  ESP_LOGW(TAG, "Failed to connect to %s within %lums", ssid, (unsigned long)timeout_ms);
  return false;
}

bool wifi_manager_is_connected() {
  return (xEventGroupGetBits(xNetworkEventGroup) & WIFI_CONNECTED_BIT) != 0;
}

void wifi_manager_suppress_reconnect(bool suppress) {
  s_suppress_reconnect = suppress;
}
