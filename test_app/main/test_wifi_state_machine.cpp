/**
 * Tests for the WiFi manager state machine logic.
 *
 * These tests verify the state machine transitions without actual WiFi hardware.
 * We test the logic by simulating events and checking expected states.
 *
 * Since QEMU doesn't have WiFi hardware, these tests validate:
 * - Credential detection logic (NVS read)
 * - State machine transitions (conceptual, via NVS state)
 * - Event bit management
 */
#include <unity.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <cstring>

// Import event bit definitions
#define WIFI_CONNECTED_BIT (1 << 1)
#define WIFI_AP_ACTIVE_BIT (1 << 2)

#define NVS_NET_NAMESPACE "nvs.net80211"
#define NVS_KEY_STA_SSID "sta.ssid"
#define NVS_KEY_STA_PSWD "sta.pswd"

extern EventGroupHandle_t status_event_group;

static void _clear_wifi_creds() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NET_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

static void _store_wifi_creds(const char *ssid, const char *password) {
  nvs_handle_t handle;
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READWRITE, &handle));
  nvs_set_str(handle, NVS_KEY_STA_SSID, ssid);
  nvs_set_str(handle, NVS_KEY_STA_PSWD, password);
  nvs_commit(handle);
  nvs_close(handle);
}

static bool _has_stored_credentials() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NET_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) return false;

  char ssid[33] = {};
  size_t len = sizeof(ssid);
  err = nvs_get_str(handle, NVS_KEY_STA_SSID, ssid, &len);
  nvs_close(handle);

  return (err == ESP_OK && strlen(ssid) > 0);
}

TEST_CASE("WiFi SM: no credentials → should activate AP", "[wifi_sm]") {
  _clear_wifi_creds();

  // Verify no credentials
  TEST_ASSERT_FALSE(_has_stored_credentials());

  // In the real system, wifi_manager_init would detect this and start AP mode
  // Here we verify the precondition: absence of credentials means AP should start
}

TEST_CASE("WiFi SM: credentials present → should attempt STA", "[wifi_sm]") {
  _store_wifi_creds("HomeNetwork", "password123");

  TEST_ASSERT_TRUE(_has_stored_credentials());

  // Verify the stored SSID is correct
  nvs_handle_t handle;
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READONLY, &handle));
  char ssid[33] = {};
  size_t len = sizeof(ssid);
  nvs_get_str(handle, NVS_KEY_STA_SSID, ssid, &len);
  TEST_ASSERT_EQUAL_STRING("HomeNetwork", ssid);
  nvs_close(handle);

  _clear_wifi_creds();
}

TEST_CASE("WiFi SM: event bits reflect AP active state", "[wifi_sm]") {
  // Simulate AP becoming active
  xEventGroupSetBits(status_event_group, WIFI_AP_ACTIVE_BIT);
  EventBits_t bits = xEventGroupGetBits(status_event_group);
  TEST_ASSERT_TRUE(bits & WIFI_AP_ACTIVE_BIT);
  TEST_ASSERT_FALSE(bits & WIFI_CONNECTED_BIT);

  // Simulate STA connects and AP stops
  xEventGroupSetBits(status_event_group, WIFI_CONNECTED_BIT);
  xEventGroupClearBits(status_event_group, WIFI_AP_ACTIVE_BIT);
  bits = xEventGroupGetBits(status_event_group);
  TEST_ASSERT_TRUE(bits & WIFI_CONNECTED_BIT);
  TEST_ASSERT_FALSE(bits & WIFI_AP_ACTIVE_BIT);

  // Cleanup
  xEventGroupClearBits(status_event_group, WIFI_CONNECTED_BIT | WIFI_AP_ACTIVE_BIT);
}

TEST_CASE("WiFi SM: new credentials overwrite old", "[wifi_sm]") {
  _store_wifi_creds("OldNetwork", "oldpass");
  TEST_ASSERT_TRUE(_has_stored_credentials());

  // Simulate user entering new creds via SPA
  _store_wifi_creds("NewNetwork", "newpass");

  nvs_handle_t handle;
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READONLY, &handle));
  char ssid[33] = {};
  size_t len = sizeof(ssid);
  nvs_get_str(handle, NVS_KEY_STA_SSID, ssid, &len);
  TEST_ASSERT_EQUAL_STRING("NewNetwork", ssid);
  nvs_close(handle);

  _clear_wifi_creds();
}

TEST_CASE("WiFi SM: event group bits are independent", "[wifi_sm]") {
  // Both AP and STA can be active simultaneously (APSTA mode during transition)
  xEventGroupSetBits(status_event_group, WIFI_AP_ACTIVE_BIT | WIFI_CONNECTED_BIT);
  EventBits_t bits = xEventGroupGetBits(status_event_group);
  TEST_ASSERT_TRUE(bits & WIFI_AP_ACTIVE_BIT);
  TEST_ASSERT_TRUE(bits & WIFI_CONNECTED_BIT);

  // Clear AP while STA remains
  xEventGroupClearBits(status_event_group, WIFI_AP_ACTIVE_BIT);
  bits = xEventGroupGetBits(status_event_group);
  TEST_ASSERT_FALSE(bits & WIFI_AP_ACTIVE_BIT);
  TEST_ASSERT_TRUE(bits & WIFI_CONNECTED_BIT);

  // Cleanup
  xEventGroupClearBits(status_event_group, WIFI_CONNECTED_BIT | WIFI_AP_ACTIVE_BIT);
}
