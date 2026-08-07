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
#include "net_diag.h"

#define TAG "webserver"
// The shared LWIP socket pool is CONFIG_LWIP_MAX_SOCKETS (24). It is split
// across everything that opens a socket, so budget the web server to leave
// room for HomeKit's HAP server, MQTT, SNTP and mDNS:
//   HAP  5 accept + 1 listen + 1 ctrl = 7
//   web  4 accept + 1 listen + 1 ctrl = 6   (this value drives the 4)
//   MQTT 1-2 + SNTP 1 + mDNS 1-2           = ~4
//   ----------------------------------------> ~17 of 24, with headroom
// Idle keep-alive connections are reaped below so a browser's spare sockets are
// released quickly instead of squatting the budget.
#define MAX_CONNECTIONS 4

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
  ESP_LOGI(TAG, "Web server started on port %d (max_open_sockets=%d, +listener +ctrl)", config.server_port,
           config.max_open_sockets);
  net_diag_dump_sockets("after web_server_start");
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
