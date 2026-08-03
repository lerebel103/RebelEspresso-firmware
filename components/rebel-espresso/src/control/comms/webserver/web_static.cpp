#include "web_static.h"

#include <esp_log.h>
#include <cstring>

#include "wifi/wifi_ap.h"

#define TAG "web_static"

// Embedded gzipped SPA — generated at build time from webapp/index.html
extern const uint8_t webapp_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t webapp_gz_end[] asm("_binary_index_html_gz_end");

/**
 * Serve the embedded SPA for all non-API requests.
 * Since it's a single-page app, all paths serve the same HTML.
 */
static esp_err_t _static_handler(httpd_req_t *req) {
  // Captive portal redirect: if AP is active and Host header is not the local IP,
  // redirect to the device's AP address so captive portal detection works
  if (wifi_ap_is_active()) {
    char host_hdr[128] = {};
    if (httpd_req_get_hdr_value_str(req, "Host", host_hdr, sizeof(host_hdr)) == ESP_OK) {
      if (strstr(host_hdr, "192.168.4.1") == NULL) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1:8080/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
      }
    }
  }

  const char *uri = req->uri;

  // Reject path traversal attempts
  if (strstr(uri, "..") != NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
    return ESP_FAIL;
  }

  // Serve the embedded gzipped SPA
  size_t gz_len = webapp_gz_end - webapp_gz_start;

  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  httpd_resp_send(req, (const char *)webapp_gz_start, gz_len);
  return ESP_OK;
}

void web_static_register(httpd_handle_t server) {
  // Wildcard catch-all — must be registered last
  const httpd_uri_t static_uri = {
      .uri = "/*",
      .method = HTTP_GET,
      .handler = _static_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &static_uri);
  ESP_LOGI(TAG, "Embedded SPA handler registered (%d bytes gzipped)", (int)(webapp_gz_end - webapp_gz_start));
}
