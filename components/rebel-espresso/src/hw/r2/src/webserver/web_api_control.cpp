#include "web_api_control.h"

#include <esp_log.h>
#include <cJSON.h>
#include <cstring>

#include "web_auth.h"
#include "power.h"
#include "brew_temp.h"
#include "homekit/homekit.h"

#define TAG "api_control"

#define BREW_TEMP_MIN 88.0
#define BREW_TEMP_MAX 96.0

// --- POST /api/control/power ---
// Body: {"active": true|false}

static esp_err_t _power_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  char body[64];
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

  cJSON *active_item = cJSON_GetObjectItem(json, "active");
  if (!active_item || !cJSON_IsBool(active_item)) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'active' boolean");
    return ESP_FAIL;
  }

  bool want_active = cJSON_IsTrue(active_item);
  cJSON_Delete(json);

  if (want_active) {
    power_active();
  } else {
    power_standby();
  }

  bool is_active = power_is_active();
  ESP_LOGI(TAG, "Power %s (requested: %s)", is_active ? "ON" : "OFF", want_active ? "ON" : "OFF");
  homekit_notify_power_changed(is_active);

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddBoolToObject(resp, "active", is_active);
  const char *resp_str = cJSON_PrintUnformatted(resp);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, resp_str);
  cJSON_free((void *)resp_str);
  cJSON_Delete(resp);
  return ESP_OK;
}

// --- POST /api/control/brew-temp ---
// Body: {"setpoint": 92.0}

static esp_err_t _brew_temp_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  char body[64];
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

  cJSON *sp_item = cJSON_GetObjectItem(json, "setpoint");
  if (!sp_item || !cJSON_IsNumber(sp_item)) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'setpoint' number");
    return ESP_FAIL;
  }

  double setpoint = sp_item->valuedouble;
  cJSON_Delete(json);

  if (setpoint < BREW_TEMP_MIN || setpoint > BREW_TEMP_MAX) {
    char err_msg[64];
    snprintf(err_msg, sizeof(err_msg), "Setpoint must be %.0f-%.0f", BREW_TEMP_MIN, BREW_TEMP_MAX);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err_msg);
    return ESP_FAIL;
  }

  brew_temp_set_setpoint(setpoint);
  double actual = brew_temp_get_setpoint();
  ESP_LOGI(TAG, "Brew temp setpoint: %.1f", actual);
  homekit_notify_setpoint_changed((float)actual);

  cJSON *resp = cJSON_CreateObject();
  cJSON_AddNumberToObject(resp, "setpoint", actual);
  cJSON_AddNumberToObject(resp, "min", BREW_TEMP_MIN);
  cJSON_AddNumberToObject(resp, "max", BREW_TEMP_MAX);
  const char *resp_str = cJSON_PrintUnformatted(resp);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, resp_str);
  cJSON_free((void *)resp_str);
  cJSON_Delete(resp);
  return ESP_OK;
}

void web_api_control_register(httpd_handle_t server) {
  const httpd_uri_t power_uri = {
      .uri = "/api/control/power",
      .method = HTTP_POST,
      .handler = _power_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &power_uri);

  const httpd_uri_t brew_temp_uri = {
      .uri = "/api/control/brew-temp",
      .method = HTTP_POST,
      .handler = _brew_temp_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &brew_temp_uri);

  ESP_LOGI(TAG, "Control API registered");
}
