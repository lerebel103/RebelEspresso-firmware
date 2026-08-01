#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

enum wifi_manager_mode_t {
  WIFI_MGR_MODE_IDLE,
  WIFI_MGR_MODE_STA,
  WIFI_MGR_MODE_AP,
  WIFI_MGR_MODE_AP_STA,
};

struct wifi_metrics_t {
  uint32_t connect_attempt_count;
  uint32_t connected_count;
  uint32_t disconnected_count;
  uint32_t connect_duration_ms;
  int8_t rssi;
  uint8_t channel;
  char ssid[33];
  char ap_bssid[33];
  char ip_addr[16];
  char gw_addr[16];
  char nm_addr[16];
};

/**
 * Initialize WiFi manager. Attempts STA connection using stored credentials.
 * Falls back to Soft-AP mode if no credentials or connection fails.
 */
void wifi_manager_init(EventGroupHandle_t networkEventGroup);

/**
 * Get current WiFi manager mode.
 */
wifi_manager_mode_t wifi_manager_get_mode();

/**
 * Get WiFi STA metrics (RSSI, IP, SSID, etc.)
 */
wifi_metrics_t wifi_manager_get_metrics();

/**
 * Attempt to connect to a new WiFi network. Stores credentials in NVS and
 * triggers a STA connection attempt.
 *
 * @param ssid Network SSID
 * @param password Network password (PSK)
 * @param timeout_ms Maximum time to wait for connection (default 10000ms)
 * @return true if connected successfully, false on timeout/failure
 */
bool wifi_manager_connect(const char *ssid, const char *password, uint32_t timeout_ms = 10000);

/**
 * Check if STA is currently connected.
 */
bool wifi_manager_is_connected();

/**
 * Temporarily suppress automatic reconnection (used during WiFi scan).
 * Call with true before disconnecting for scan, false after scan completes.
 */
void wifi_manager_suppress_reconnect(bool suppress);
