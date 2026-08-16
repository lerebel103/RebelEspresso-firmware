#include "wifi_ap.h"

#include "dns_server.h"
#include "common/events_common.h"

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_wifi.h>

#include <cstring>

#define TAG "wifi_ap"

#define AP_IP_ADDR "192.168.4.1"
#define AP_GW_ADDR "192.168.4.1"
#define AP_NETMASK "255.255.255.0"

static bool s_ap_active = false;
static esp_netif_t *s_ap_netif = nullptr;
static wifi_ap_info_t s_ap_info = {};
static char s_ap_password[9] = {};

static void _generate_ap_password() {
  static const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  uint8_t random_bytes[8];
  esp_fill_random(random_bytes, sizeof(random_bytes));
  for (size_t i = 0; i < 8; i++) {
    s_ap_password[i] = kAlphabet[random_bytes[i] % (sizeof(kAlphabet) - 1)];
  }
  s_ap_password[8] = '\0';
}

static void _ap_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
      wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
      ESP_LOGI(TAG, "Client connected (MAC: " MACSTR ", AID=%d)", MAC2STR(event->mac), event->aid);
      s_ap_info.client_count++;
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
      wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
      ESP_LOGI(TAG, "Client disconnected (MAC: " MACSTR ", AID=%d)", MAC2STR(event->mac), event->aid);
      if (s_ap_info.client_count > 0) {
        s_ap_info.client_count--;
      }
    }
  }
}

void wifi_ap_start(EventGroupHandle_t networkEventGroup) {
  if (s_ap_active) {
    ESP_LOGW(TAG, "AP already active");
    return;
  }

  ESP_LOGI(TAG, "Starting Soft-AP...");

  // Register AP event handler
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &_ap_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &_ap_event_handler, NULL));

  // Create AP netif if not already created
  if (s_ap_netif == nullptr) {
    s_ap_netif = esp_netif_create_default_wifi_ap();
  }

  // Build SSID: "RebelEspresso-XXXXXX"
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  snprintf(s_ap_info.ssid, sizeof(s_ap_info.ssid), "%s-%02X%02X%02X", CONFIG_WIFI_AP_SSID_PREFIX, mac[3], mac[4],
           mac[5]);

  _generate_ap_password();

  // Configure AP
  wifi_config_t ap_config = {};
  strncpy((char *)ap_config.ap.ssid, s_ap_info.ssid, sizeof(ap_config.ap.ssid) - 1);
  strncpy((char *)ap_config.ap.password, s_ap_password, sizeof(ap_config.ap.password) - 1);
  ap_config.ap.ssid_len = strlen(s_ap_info.ssid);
  ap_config.ap.channel = CONFIG_WIFI_AP_CHANNEL;
  ap_config.ap.max_connection = CONFIG_WIFI_AP_MAX_CONNECTIONS;
  ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ap_config.ap.ssid_hidden = 0;

  // Switch to AP+STA mode so scanning still works
  wifi_mode_t current_mode;
  esp_wifi_get_mode(&current_mode);
  if (current_mode == WIFI_MODE_STA) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  } else if (current_mode == WIFI_MODE_NULL) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  }

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

  // If WiFi wasn't started yet, start it. If already started, that's fine.
  esp_err_t err = esp_wifi_start();
  if (err != ESP_OK && err != ESP_ERR_WIFI_STATE && err != ESP_ERR_WIFI_CONN) {
    ESP_ERROR_CHECK(err);
  }

  s_ap_active = true;
  s_ap_info.client_count = 0;

  xEventGroupSetBits(networkEventGroup, WIFI_AP_ACTIVE_BIT);

  // Start captive portal DNS server
  dns_server_start(AP_IP_ADDR);

  ESP_LOGI(TAG, "Soft-AP started: SSID='%s', IP=%s", s_ap_info.ssid, AP_IP_ADDR);
}

void wifi_ap_stop(EventGroupHandle_t networkEventGroup) {
  if (!s_ap_active) {
    return;
  }

  ESP_LOGI(TAG, "Stopping Soft-AP...");

  // Stop DNS server
  dns_server_stop();

  // Unregister AP event handlers
  esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &_ap_event_handler);
  esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &_ap_event_handler);

  // Switch back to STA-only mode
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  s_ap_active = false;
  s_ap_info.client_count = 0;
  memset(s_ap_password, 0, sizeof(s_ap_password));

  xEventGroupClearBits(networkEventGroup, WIFI_AP_ACTIVE_BIT);

  ESP_LOGI(TAG, "Soft-AP stopped");
}

bool wifi_ap_is_active() {
  return s_ap_active;
}

wifi_ap_info_t wifi_ap_get_info() {
  return s_ap_info;
}

bool wifi_ap_get_password(char *out, size_t len) {
  if (out == nullptr || len == 0 || s_ap_password[0] == '\0') {
    return false;
  }

  size_t copy_len = strlen(s_ap_password);
  if (copy_len > (len - 1)) {
    copy_len = len - 1;
  }
  memcpy(out, s_ap_password, copy_len);
  out[copy_len] = '\0';
  return true;
}
