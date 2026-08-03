#include "web_auth.h"

#include <esp_log.h>
#include <esp_random.h>
#include <nvs.h>
#include <mbedtls/md.h>
#include <cJSON.h>
#include <cstring>

#include "wifi/wifi_ap.h"

#define TAG "web_auth"
#define NVS_NAMESPACE "sys"
#define NVS_KEY_AUTH_ENABLED "http_auth_en"
#define NVS_KEY_AUTH_HASH "http_auth_hash"
#define SHA256_HEX_LEN 64
#define TOKEN_LEN 32

static bool s_auth_enabled = false;
static char s_password_hash[SHA256_HEX_LEN + 1] = {};

// Session token — generated on successful login, invalidated on password change/disable
static char s_session_token[TOKEN_LEN * 2 + 1] = {};
static bool s_token_valid = false;

static void _load_auth_state() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    return;
  }

  uint8_t enabled = 0;
  nvs_get_u8(handle, NVS_KEY_AUTH_ENABLED, &enabled);
  s_auth_enabled = (enabled != 0);

  if (s_auth_enabled) {
    size_t len = sizeof(s_password_hash);
    esp_err_t err = nvs_get_str(handle, NVS_KEY_AUTH_HASH, s_password_hash, &len);
    if (err != ESP_OK || strlen(s_password_hash) != SHA256_HEX_LEN) {
      ESP_LOGW(TAG, "Auth hash missing/corrupt — disabling auth");
      s_auth_enabled = false;
      memset(s_password_hash, 0, sizeof(s_password_hash));
    }
  }

  nvs_close(handle);
}

static void _sha256_hex(const char *input, char *output) {
  unsigned char hash[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char *)input, strlen(input));
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);

  for (int i = 0; i < 32; i++) {
    sprintf(output + i * 2, "%02x", hash[i]);
  }
  output[64] = '\0';
}

static void _generate_token() {
  uint8_t random_bytes[TOKEN_LEN];
  esp_fill_random(random_bytes, TOKEN_LEN);
  for (int i = 0; i < TOKEN_LEN; i++) {
    sprintf(s_session_token + i * 2, "%02x", random_bytes[i]);
  }
  s_session_token[TOKEN_LEN * 2] = '\0';
  s_token_valid = true;
}

static void _invalidate_token() {
  memset(s_session_token, 0, sizeof(s_session_token));
  s_token_valid = false;
}

bool web_auth_check(httpd_req_t *req) {
  if (!s_auth_enabled) {
    return true;
  }

  // Bypass auth when Soft-AP is active (captive portal onboarding mode)
  if (wifi_ap_is_active()) {
    return true;
  }

  // Check for Bearer token
  char auth_header[128] = {};
  if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
    return false;
  }

  // Parse "Bearer <token>"
  if (strncmp(auth_header, "Bearer ", 7) != 0) {
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid auth scheme");
    return false;
  }

  const char *token = auth_header + 7;
  if (!s_token_valid || strcmp(token, s_session_token) != 0) {
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid or expired token");
    return false;
  }

  return true;
}

// --- GET /api/auth (check status — no auth required) ---

static esp_err_t _auth_get_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "enabled", s_auth_enabled);

  const char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, json);

  cJSON_free((void *)json);
  cJSON_Delete(root);
  return ESP_OK;
}

// --- POST /api/auth/login (authenticate with password, get token) ---

static esp_err_t _auth_login_handler(httpd_req_t *req) {
  if (!s_auth_enabled) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"token\":\"\",\"message\":\"Auth not enabled\"}");
    return ESP_OK;
  }

  char body[256];
  int len = req->content_len;
  if (len <= 0 || len >= (int)sizeof(body)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    return ESP_FAIL;
  }
  int received = 0;
  while (received < len) {
    int ret = httpd_req_recv(req, body + received, len - received);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        continue;
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Receive error");
      return ESP_FAIL;
    }
    received += ret;
  }
  body[received] = '\0';

  cJSON *json = cJSON_Parse(body);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *pw = cJSON_GetObjectItem(json, "password");
  if (!pw || !cJSON_IsString(pw)) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing password");
    return ESP_FAIL;
  }

  // Verify password
  char hash[SHA256_HEX_LEN + 1];
  _sha256_hex(pw->valuestring, hash);
  cJSON_Delete(json);

  if (strcmp(hash, s_password_hash) != 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Invalid password\"}");
    return ESP_OK;
  }

  // Generate session token
  _generate_token();

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "success", true);
  cJSON_AddStringToObject(resp, "token", s_session_token);

  const char *resp_str = cJSON_PrintUnformatted(resp);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, resp_str);

  cJSON_free((void *)resp_str);
  cJSON_Delete(resp);
  return ESP_OK;
}

// --- POST /api/auth (set password — requires current auth) ---

static esp_err_t _auth_post_handler(httpd_req_t *req) {
  // If auth is already enabled, require valid token to change password
  if (s_auth_enabled && !web_auth_check(req)) {
    return ESP_FAIL;
  }

  char body[256];
  int len = req->content_len;
  if (len <= 0 || len >= (int)sizeof(body)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    return ESP_FAIL;
  }
  int received = 0;
  while (received < len) {
    int ret = httpd_req_recv(req, body + received, len - received);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        continue;
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Receive error");
      return ESP_FAIL;
    }
    received += ret;
  }
  body[received] = '\0';

  cJSON *json = cJSON_Parse(body);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *pw = cJSON_GetObjectItem(json, "password");
  if (!pw || !cJSON_IsString(pw) || strlen(pw->valuestring) < 4) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password must be at least 4 characters");
    return ESP_FAIL;
  }

  // Hash and store
  _sha256_hex(pw->valuestring, s_password_hash);
  s_auth_enabled = true;
  cJSON_Delete(json);

  // Invalidate old token, generate new one
  _generate_token();

  nvs_handle_t handle;
  ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
  uint8_t enabled = 1;
  nvs_set_u8(handle, NVS_KEY_AUTH_ENABLED, enabled);
  nvs_set_str(handle, NVS_KEY_AUTH_HASH, s_password_hash);
  nvs_commit(handle);
  nvs_close(handle);

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddStringToObject(resp, "status", "ok");
  cJSON_AddStringToObject(resp, "message", "Password set, auth enabled");
  cJSON_AddStringToObject(resp, "token", s_session_token);

  const char *resp_str = cJSON_PrintUnformatted(resp);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, resp_str);

  cJSON_free((void *)resp_str);
  cJSON_Delete(resp);
  return ESP_OK;
}

// --- DELETE /api/auth (disable auth — requires current auth) ---

static esp_err_t _auth_delete_handler(httpd_req_t *req) {
  if (!web_auth_check(req)) {
    return ESP_FAIL;
  }

  s_auth_enabled = false;
  memset(s_password_hash, 0, sizeof(s_password_hash));
  _invalidate_token();

  nvs_handle_t handle;
  ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
  uint8_t enabled = 0;
  nvs_set_u8(handle, NVS_KEY_AUTH_ENABLED, enabled);
  nvs_erase_key(handle, NVS_KEY_AUTH_HASH);
  nvs_commit(handle);
  nvs_close(handle);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Auth disabled\"}");
  return ESP_OK;
}

void web_auth_register(httpd_handle_t server) {
  _load_auth_state();

  const httpd_uri_t get_uri = {
      .uri = "/api/auth",
      .method = HTTP_GET,
      .handler = _auth_get_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &get_uri);

  const httpd_uri_t login_uri = {
      .uri = "/api/auth/login",
      .method = HTTP_POST,
      .handler = _auth_login_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &login_uri);

  const httpd_uri_t post_uri = {
      .uri = "/api/auth",
      .method = HTTP_POST,
      .handler = _auth_post_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &post_uri);

  const httpd_uri_t delete_uri = {
      .uri = "/api/auth",
      .method = HTTP_DELETE,
      .handler = _auth_delete_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &delete_uri);

  ESP_LOGI(TAG, "Auth API registered (auth %s)", s_auth_enabled ? "enabled" : "disabled");
}
