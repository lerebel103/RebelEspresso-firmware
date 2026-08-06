#include "web_api_wifi.h"

#include <esp_log.h>
#include <cJSON.h>
#include <cstring>

#include "web_auth.h"
#include "wifi/wifi_scan.h"
#include "wifi/wifi_manager.h"
#include "wifi/wifi_ap.h"

#define TAG "api_wifi"

// --- GET /api/wifi/scan ---

static esp_err_t _wifi_scan_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  wifi_scan_list_t scan = wifi_scan_perform();

  cJSON *root = cJSON_CreateObject();
  cJSON *networks = cJSON_AddArrayToObject(root, "networks");

  for (int i = 0; i < scan.count; i++) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "ssid", scan.results[i].ssid);
    cJSON_AddNumberToObject(item, "rssi", scan.results[i].rssi);
    cJSON_AddStringToObject(item, "auth", scan.results[i].auth);
    cJSON_AddNumberToObject(item, "channel", scan.results[i].channel);
    cJSON_AddItemToArray(networks, item);
  }

  const char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, json);

  cJSON_free((void *)json);
  cJSON_Delete(root);
  return ESP_OK;
}

// --- GET /api/wifi/status ---

static esp_err_t _wifi_status_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  cJSON *root = cJSON_CreateObject();

  wifi_manager_mode_t mode = wifi_manager_get_mode();
  const char *mode_str = "idle";
  switch (mode) {
    case WIFI_MGR_MODE_STA:
      mode_str = "sta";
      break;
    case WIFI_MGR_MODE_AP:
      mode_str = "ap";
      break;
    case WIFI_MGR_MODE_AP_STA:
      mode_str = "ap+sta";
      break;
    default:
      break;
  }
  cJSON_AddStringToObject(root, "mode", mode_str);

  // STA info
  cJSON *sta = cJSON_AddObjectToObject(root, "sta");
  bool connected = wifi_manager_is_connected();
  cJSON_AddBoolToObject(sta, "connected", connected);

  if (connected) {
    wifi_metrics_t metrics = wifi_manager_get_metrics();
    cJSON_AddStringToObject(sta, "ssid", metrics.ssid);
    cJSON_AddStringToObject(sta, "ip", metrics.ip_addr);
    cJSON_AddNumberToObject(sta, "rssi", metrics.rssi);
  } else {
    cJSON_AddStringToObject(sta, "ssid", "");
    cJSON_AddStringToObject(sta, "ip", "");
    cJSON_AddNumberToObject(sta, "rssi", 0);
  }

  // AP info
  cJSON *ap = cJSON_AddObjectToObject(root, "ap");
  bool ap_active = wifi_ap_is_active();
  cJSON_AddBoolToObject(ap, "active", ap_active);

  if (ap_active) {
    wifi_ap_info_t ap_info = wifi_ap_get_info();
    cJSON_AddStringToObject(ap, "ssid", ap_info.ssid);
    cJSON_AddNumberToObject(ap, "clients", ap_info.client_count);
  } else {
    cJSON_AddStringToObject(ap, "ssid", "");
    cJSON_AddNumberToObject(ap, "clients", 0);
  }

  const char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, json);

  cJSON_free((void *)json);
  cJSON_Delete(root);
  return ESP_OK;
}

// --- POST /api/wifi/connect ---

static esp_err_t _wifi_connect_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

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

  cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
  if (!ssid_item || !cJSON_IsString(ssid_item) || strlen(ssid_item->valuestring) == 0) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or empty 'ssid'");
    return ESP_FAIL;
  }

  const char *ssid = ssid_item->valuestring;
  const char *psk = "";

  cJSON *psk_item = cJSON_GetObjectItem(json, "psk");
  if (psk_item && cJSON_IsString(psk_item)) {
    psk = psk_item->valuestring;
  }

  ESP_LOGI(TAG, "WiFi connect request for SSID: %s", ssid);

  // Attempt connection (blocks for up to 10s)
  bool success = wifi_manager_connect(ssid, psk, 10000);

  cJSON *response = cJSON_CreateObject();
  cJSON_AddBoolToObject(response, "success", success);

  if (success) {
    wifi_metrics_t metrics = wifi_manager_get_metrics();
    cJSON_AddStringToObject(response, "ip", metrics.ip_addr);
  } else {
    cJSON_AddStringToObject(response, "error", "connection_failed");
  }

  const char *resp_str = cJSON_PrintUnformatted(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, resp_str);

  cJSON_free((void *)resp_str);
  cJSON_Delete(response);
  cJSON_Delete(json);
  return ESP_OK;
}

// --- GET /api/wifi/hostname ---

static esp_err_t _wifi_hostname_get_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  char hostname[64];
  wifi_manager_get_hostname(hostname, sizeof(hostname));

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "hostname", hostname);

  const char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, json);

  cJSON_free((void *)json);
  cJSON_Delete(root);
  return ESP_OK;
}

// --- POST /api/wifi/hostname ---

static esp_err_t _wifi_hostname_set_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  char body[128];
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

  cJSON *host_item = cJSON_GetObjectItem(json, "hostname");
  if (!host_item || !cJSON_IsString(host_item)) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'hostname'");
    return ESP_FAIL;
  }

  wifi_manager_set_hostname(host_item->valuestring);
  cJSON_Delete(json);

  // Echo back the sanitised value that was actually applied.
  char applied[64];
  wifi_manager_get_hostname(applied, sizeof(applied));

  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "status", "ok");
  cJSON_AddStringToObject(response, "hostname", applied);

  const char *resp_str = cJSON_PrintUnformatted(response);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, resp_str);

  cJSON_free((void *)resp_str);
  cJSON_Delete(response);
  return ESP_OK;
}

// --- OPTIONS handler for CORS preflight ---

static esp_err_t _wifi_options_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
  httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

void web_api_wifi_register(httpd_handle_t server) {
  const httpd_uri_t scan_uri = {
      .uri = "/api/wifi/scan",
      .method = HTTP_GET,
      .handler = _wifi_scan_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &scan_uri);

  const httpd_uri_t status_uri = {
      .uri = "/api/wifi/status",
      .method = HTTP_GET,
      .handler = _wifi_status_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &status_uri);

  const httpd_uri_t connect_uri = {
      .uri = "/api/wifi/connect",
      .method = HTTP_POST,
      .handler = _wifi_connect_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &connect_uri);

  // CORS preflight for connect endpoint
  const httpd_uri_t connect_options = {
      .uri = "/api/wifi/connect",
      .method = HTTP_OPTIONS,
      .handler = _wifi_options_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &connect_options);

  const httpd_uri_t hostname_get_uri = {
      .uri = "/api/wifi/hostname",
      .method = HTTP_GET,
      .handler = _wifi_hostname_get_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &hostname_get_uri);

  const httpd_uri_t hostname_set_uri = {
      .uri = "/api/wifi/hostname",
      .method = HTTP_POST,
      .handler = _wifi_hostname_set_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &hostname_set_uri);

  const httpd_uri_t hostname_options = {
      .uri = "/api/wifi/hostname",
      .method = HTTP_OPTIONS,
      .handler = _wifi_options_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &hostname_options);

  ESP_LOGI(TAG, "WiFi API registered");
}
