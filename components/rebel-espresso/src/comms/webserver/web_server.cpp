#include "web_server.h"

#include <esp_log.h>
#include <esp_http_server.h>
#include <cstring>

#include "web_static.h"
#include "web_api_status.h"
#include "web_api_config.h"
#include "web_api_system.h"
#include "web_api_control.h"
#include "web_api_wifi.h"
#include "web_auth.h"

#define TAG "webserver"
// Browsers open up to ~6 persistent connections per origin; give one spare so a
// new request never LRU-purges an active one. Budget: 7+3 internal < LWIP 16.
#define MAX_CONNECTIONS 7

static httpd_handle_t s_server = nullptr;
static bool s_running = false;

esp_err_t web_server_start() {
  if (s_running) {
    ESP_LOGW(TAG, "Web server already running");
    return ESP_OK;
  }

  // Configure and start HTTP server
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080; // Port 80 is used by HomeKit
  config.max_uri_handlers = 32;
  config.max_open_sockets = MAX_CONNECTIONS;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;
  config.stack_size = 8192;

  // Reap dead/half-open keep-alive connections so idle browser sockets are
  // released instead of lingering until an LRU purge kicks an active request
  // ("server unexpectedly closed the connection"). Drops after ~20 s of silence.
  config.keep_alive_enable = true;
  config.keep_alive_idle = 5;
  config.keep_alive_interval = 5;
  config.keep_alive_count = 3;

  esp_err_t err = httpd_start(&s_server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    return err;
  }

  // Register API handlers
  web_api_status_register(s_server);
  web_api_config_register(s_server);
  web_api_system_register(s_server);
  web_api_control_register(s_server);
  web_api_wifi_register(s_server);
  web_auth_register(s_server);

  // Register static file handler last (wildcard catch-all for SPA)
  web_static_register(s_server);

  s_running = true;
  ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
  return ESP_OK;
}

void web_server_stop() {
  if (!s_running) {
    return;
  }

  if (s_server) {
    httpd_stop(s_server);
    s_server = nullptr;
  }

  s_running = false;
  ESP_LOGI(TAG, "Web server stopped");
}

bool web_server_is_running() {
  return s_running;
}
