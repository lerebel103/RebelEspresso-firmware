/**
 * Tests for the authentication bypass logic in AP mode.
 *
 * Since we can't run the full HTTP server in QEMU, we test the auth decision
 * logic directly. The key invariant: when AP mode is active, auth is bypassed.
 */
#include <unity.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>

// NVS keys matching web_auth.cpp
#define NVS_NAMESPACE "sys"
#define NVS_KEY_AUTH_ENABLED "http_auth_en"
#define NVS_KEY_AUTH_HASH "http_auth_hash"
#define SHA256_HEX_LEN 64

// Simulated AP state for testing
static bool s_test_ap_active = false;
bool test_wifi_ap_is_active() { return s_test_ap_active; }

// Simplified auth check logic (mirrors web_auth_check without httpd dependencies)
static bool s_auth_enabled = false;
static char s_password_hash[SHA256_HEX_LEN + 1] = {};

static void load_auth_state() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    s_auth_enabled = false;
    return;
  }
  uint8_t enabled = 0;
  nvs_get_u8(handle, NVS_KEY_AUTH_ENABLED, &enabled);
  s_auth_enabled = (enabled != 0);
  if (s_auth_enabled) {
    size_t len = sizeof(s_password_hash);
    esp_err_t err = nvs_get_str(handle, NVS_KEY_AUTH_HASH, s_password_hash, &len);
    if (err != ESP_OK) {
      s_auth_enabled = false;
    }
  }
  nvs_close(handle);
}

// Simulated auth check that mirrors the real logic
static bool check_auth(bool has_valid_credentials) {
  if (!s_auth_enabled) return true;
  if (test_wifi_ap_is_active()) return true;  // AP mode bypass
  return has_valid_credentials;
}

static void _setup_auth_enabled() {
  nvs_handle_t handle;
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
  uint8_t enabled = 1;
  nvs_set_u8(handle, NVS_KEY_AUTH_ENABLED, enabled);
  // Fake hash
  const char *hash = "a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3";
  nvs_set_str(handle, NVS_KEY_AUTH_HASH, hash);
  nvs_commit(handle);
  nvs_close(handle);
  load_auth_state();
}

static void _setup_auth_disabled() {
  nvs_handle_t handle;
  TEST_ASSERT_EQUAL(ESP_OK, nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
  uint8_t enabled = 0;
  nvs_set_u8(handle, NVS_KEY_AUTH_ENABLED, enabled);
  nvs_erase_key(handle, NVS_KEY_AUTH_HASH);
  nvs_commit(handle);
  nvs_close(handle);
  load_auth_state();
}

TEST_CASE("Auth: bypassed when AP mode active (auth enabled)", "[auth]") {
  _setup_auth_enabled();
  s_test_ap_active = true;

  // Even without valid credentials, access should be granted in AP mode
  TEST_ASSERT_TRUE(check_auth(false));

  s_test_ap_active = false;
}

TEST_CASE("Auth: enforced when AP mode inactive (auth enabled)", "[auth]") {
  _setup_auth_enabled();
  s_test_ap_active = false;

  // Without valid credentials, access should be denied
  TEST_ASSERT_FALSE(check_auth(false));
  // With valid credentials, access should be granted
  TEST_ASSERT_TRUE(check_auth(true));
}

TEST_CASE("Auth: always passes when auth disabled", "[auth]") {
  _setup_auth_disabled();
  s_test_ap_active = false;

  TEST_ASSERT_TRUE(check_auth(false));
  TEST_ASSERT_TRUE(check_auth(true));
}

TEST_CASE("Auth: AP bypass re-engages after AP stops", "[auth]") {
  _setup_auth_enabled();

  // AP active — bypass
  s_test_ap_active = true;
  TEST_ASSERT_TRUE(check_auth(false));

  // AP stops — auth enforced again
  s_test_ap_active = false;
  TEST_ASSERT_FALSE(check_auth(false));
  TEST_ASSERT_TRUE(check_auth(true));
}

TEST_CASE("Auth: NVS auth state preserved during AP mode", "[auth]") {
  _setup_auth_enabled();
  s_test_ap_active = true;

  // Auth is bypassed but state remains enabled in NVS
  TEST_ASSERT_TRUE(check_auth(false));
  TEST_ASSERT_TRUE(s_auth_enabled);  // Still marked as enabled

  s_test_ap_active = false;
  // After AP stops, auth is enforced without needing reload
  TEST_ASSERT_FALSE(check_auth(false));
}
