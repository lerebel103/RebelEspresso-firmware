#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <stddef.h>

struct wifi_ap_info_t {
  char ssid[33];
  uint8_t client_count;
};

/**
 * Start Soft-AP mode with SSID "RebelEspresso-XXXXXX" and a generated
 * onboarding password.
 * Also starts the captive portal DNS server.
 */
void wifi_ap_start(EventGroupHandle_t networkEventGroup);

/**
 * Stop Soft-AP mode and captive portal DNS server.
 */
void wifi_ap_stop(EventGroupHandle_t networkEventGroup);

/**
 * Check if Soft-AP is currently active.
 */
bool wifi_ap_is_active();

/**
 * Get Soft-AP info (SSID, client count).
 */
wifi_ap_info_t wifi_ap_get_info();

/**
 * Copy the current onboarding password into `out`.
 * Returns true when AP credentials have been generated.
 */
bool wifi_ap_get_password(char *out, size_t len);
