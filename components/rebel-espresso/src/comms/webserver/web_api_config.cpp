#include "web_api_config.h"

#include <esp_log.h>
#include <cJSON.h>
#include <cstring>

#include "web_auth.h"
#include "boiler_temp.h"
#include "brew_temp.h"
#include "boiler_refill.h"
#include "schedules.h"
#include "mqtt/mqtt_config.h"
#include "homekit/homekit_config.h"

#define TAG "api_config"
#define MAX_BODY_SIZE 1024

/**
 * Read request body into a buffer (loops until complete). Returns length read, or -1 on error.
 */
static int _read_body(httpd_req_t *req, char *buf, size_t max_len) {
  int total = req->content_len;
  if (total <= 0 || (size_t)total >= max_len) {
    return -1;
  }
  int received = 0;
  while (received < total) {
    int ret = httpd_req_recv(req, buf + received, total - received);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        continue;
      return -1;
    }
    received += ret;
  }
  buf[received] = '\0';
  return received;
}

/**
 * Extract the config name from URI: /api/config/<name> or /api/config/<name>/reset
 * Strips query strings and path suffixes.
 */
static bool _parse_config_name(const char *uri, char *name, size_t name_len) {
  const char *prefix = "/api/config/";
  if (strncmp(uri, prefix, strlen(prefix)) != 0) {
    return false;
  }
  const char *start = uri + strlen(prefix);
  // End at '/', '?', or end of string
  const char *end = start;
  while (*end && *end != '/' && *end != '?')
    end++;
  size_t len = (size_t)(end - start);
  if (len == 0 || len >= name_len) {
    return false;
  }
  memcpy(name, start, len);
  name[len] = '\0';
  return true;
}

// --- GET /api/config/:name ---

static esp_err_t _config_get_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  char name[32];
  if (!_parse_config_name(req->uri, name, sizeof(name))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid config name");
    return ESP_FAIL;
  }

  cJSON *json = cJSON_CreateObject();
  bool found = true;

  if (strcmp(name, "boiler_temp") == 0) {
    auto cfg = boiler_temp_get_cfg();
    cfg.to_json(json, "");
  } else if (strcmp(name, "brew_temp") == 0) {
    auto cfg = brew_temp_get_cfg();
    cfg.to_json(json, "");
  } else if (strcmp(name, "boiler_refill") == 0) {
    auto cfg = boiler_refill_get_cfg();
    cfg.to_json(json, "");
  } else if (strcmp(name, "schedules") == 0) {
    auto cfg = schedules_get_cfg();
    cfg.to_json(json, "");
  } else if (strcmp(name, "mqtt") == 0) {
    mqtt_config_get().to_json(json, "");
  } else if (strcmp(name, "homekit") == 0) {
    homekit_config_get().to_json(json, "");
  } else {
    found = false;
  }

  if (!found) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown config section");
    return ESP_FAIL;
  }

  const char *resp = cJSON_PrintUnformatted(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, resp);

  cJSON_free((void *)resp);
  cJSON_Delete(json);
  return ESP_OK;
}

// --- PUT /api/config/:name ---

static esp_err_t _config_put_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  char name[32];
  if (!_parse_config_name(req->uri, name, sizeof(name))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid config name");
    return ESP_FAIL;
  }

  char body[MAX_BODY_SIZE];
  if (_read_body(req, body, sizeof(body)) < 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large or empty");
    return ESP_FAIL;
  }

  cJSON *json = cJSON_Parse(body);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  bool found = true;
  if (strcmp(name, "boiler_temp") == 0) {
    boiler_temp_update_cfg(json);
  } else if (strcmp(name, "brew_temp") == 0) {
    brew_temp_update_cfg(json);
  } else if (strcmp(name, "boiler_refill") == 0) {
    boiler_refill_update_cfg(json);
  } else if (strcmp(name, "schedules") == 0) {
    schedules_update_cfg(json);
  } else if (strcmp(name, "mqtt") == 0) {
    mqtt_config_update(json);
  } else if (strcmp(name, "homekit") == 0) {
    homekit_config_update(json);
  } else {
    found = false;
  }

  cJSON_Delete(json);

  if (!found) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown config section");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  return ESP_OK;
}

// --- POST /api/config/:name/reset ---

static esp_err_t _config_reset_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  char name[32];
  if (!_parse_config_name(req->uri, name, sizeof(name))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid config name");
    return ESP_FAIL;
  }

  bool found = true;
  if (strcmp(name, "boiler_temp") == 0) {
    boiler_temp_reset_cfg();
  } else if (strcmp(name, "brew_temp") == 0) {
    brew_temp_reset_cfg();
  } else if (strcmp(name, "boiler_refill") == 0) {
    boiler_refill_reset_cfg();
  } else if (strcmp(name, "schedules") == 0) {
    schedules_reset_cfg();
  } else if (strcmp(name, "mqtt") == 0) {
    mqtt_config_reset();
  } else if (strcmp(name, "homekit") == 0) {
    homekit_config_reset();
  } else {
    found = false;
  }

  if (!found) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown config section");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"reset to defaults\"}");
  return ESP_OK;
}

void web_api_config_register(httpd_handle_t server) {
  const httpd_uri_t get_uri = {
      .uri = "/api/config/*",
      .method = HTTP_GET,
      .handler = _config_get_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &get_uri);

  const httpd_uri_t put_uri = {
      .uri = "/api/config/*",
      .method = HTTP_PUT,
      .handler = _config_put_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &put_uri);

  const httpd_uri_t reset_uri = {
      .uri = "/api/config/*/reset",
      .method = HTTP_POST,
      .handler = _config_reset_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &reset_uri);

  ESP_LOGI(TAG, "Config API registered");
}
