/**
 * Tests for WiFi credential NVS storage.
 *
 * Verifies that credentials are stored in the correct NVS namespace and keys
 * (nvs.net80211, sta.ssid, sta.pswd) for backward compatibility with
 * Espressif's provisioning manager.
 */
#include <unity.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>

#define NVS_NET_NAMESPACE "nvs.net80211"
#define NVS_KEY_STA_SSID "sta.ssid"
#define NVS_KEY_STA_PSWD "sta.pswd"

static void _clear_wifi_nvs() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NET_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

TEST_CASE("WiFi NVS: store and read credentials", "[wifi_creds]") {
  _clear_wifi_nvs();

  // Store credentials
  nvs_handle_t handle;
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READWRITE, &handle));
  TEST_ASSERT_EQUAL(ESP_OK, nvs_set_str(handle, NVS_KEY_STA_SSID, "TestNetwork"));
  TEST_ASSERT_EQUAL(ESP_OK, nvs_set_str(handle, NVS_KEY_STA_PSWD, "TestPassword123"));
  TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
  nvs_close(handle);

  // Read them back
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READONLY, &handle));

  char ssid[33] = {};
  size_t len = sizeof(ssid);
  TEST_ASSERT_EQUAL(ESP_OK, nvs_get_str(handle, NVS_KEY_STA_SSID, ssid, &len));
  TEST_ASSERT_EQUAL_STRING("TestNetwork", ssid);

  char password[65] = {};
  len = sizeof(password);
  TEST_ASSERT_EQUAL(ESP_OK, nvs_get_str(handle, NVS_KEY_STA_PSWD, password, &len));
  TEST_ASSERT_EQUAL_STRING("TestPassword123", password);

  nvs_close(handle);
  _clear_wifi_nvs();
}

TEST_CASE("WiFi NVS: empty namespace returns not found", "[wifi_creds]") {
  _clear_wifi_nvs();

  nvs_handle_t handle;
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READONLY, &handle));

  char ssid[33] = {};
  size_t len = sizeof(ssid);
  esp_err_t err = nvs_get_str(handle, NVS_KEY_STA_SSID, ssid, &len);
  TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, err);

  nvs_close(handle);
}

TEST_CASE("WiFi NVS: overwrite credentials", "[wifi_creds]") {
  _clear_wifi_nvs();

  nvs_handle_t handle;

  // Write first set
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READWRITE, &handle));
  nvs_set_str(handle, NVS_KEY_STA_SSID, "OldNetwork");
  nvs_set_str(handle, NVS_KEY_STA_PSWD, "OldPass");
  nvs_commit(handle);
  nvs_close(handle);

  // Overwrite
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READWRITE, &handle));
  nvs_set_str(handle, NVS_KEY_STA_SSID, "NewNetwork");
  nvs_set_str(handle, NVS_KEY_STA_PSWD, "NewPass");
  nvs_commit(handle);
  nvs_close(handle);

  // Verify new values
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READONLY, &handle));
  char ssid[33] = {};
  size_t len = sizeof(ssid);
  nvs_get_str(handle, NVS_KEY_STA_SSID, ssid, &len);
  TEST_ASSERT_EQUAL_STRING("NewNetwork", ssid);

  char password[65] = {};
  len = sizeof(password);
  nvs_get_str(handle, NVS_KEY_STA_PSWD, password, &len);
  TEST_ASSERT_EQUAL_STRING("NewPass", password);

  nvs_close(handle);
  _clear_wifi_nvs();
}

TEST_CASE("WiFi NVS: open network (empty password)", "[wifi_creds]") {
  _clear_wifi_nvs();

  nvs_handle_t handle;
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READWRITE, &handle));
  nvs_set_str(handle, NVS_KEY_STA_SSID, "OpenCafe");
  nvs_set_str(handle, NVS_KEY_STA_PSWD, "");
  nvs_commit(handle);
  nvs_close(handle);

  // Verify
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NET_NAMESPACE, NVS_READONLY, &handle));
  char password[65] = {};
  size_t len = sizeof(password);
  nvs_get_str(handle, NVS_KEY_STA_PSWD, password, &len);
  TEST_ASSERT_EQUAL_STRING("", password);

  nvs_close(handle);
  _clear_wifi_nvs();
}
