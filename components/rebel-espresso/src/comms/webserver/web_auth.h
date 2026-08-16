#pragma once
#include <esp_http_server.h>

void web_auth_register(httpd_handle_t server);

/**
 * Check if the request is authenticated. Returns true if auth is disabled,
 * temporarily bypassed for AP recovery, or if valid credentials are provided.
 * Call this at the start of protected API handlers.
 */
bool web_auth_check(httpd_req_t *req);
