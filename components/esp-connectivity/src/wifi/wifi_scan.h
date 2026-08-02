#pragma once

#include <stdint.h>

#define WIFI_SCAN_MAX_RESULTS 20

struct wifi_scan_result_t {
  char ssid[33];
  int8_t rssi;
  char auth[8]; // "open", "wep", "wpa", "wpa2", "wpa3"
  uint8_t channel;
};

struct wifi_scan_list_t {
  wifi_scan_result_t results[WIFI_SCAN_MAX_RESULTS];
  uint16_t count;
};

/**
 * Perform a WiFi scan (blocking). Returns a list of available networks
 * sorted by signal strength (strongest first), deduplicated by SSID.
 */
wifi_scan_list_t wifi_scan_perform();
