#pragma once
#include <esp_http_server.h>
#include <stddef.h>

void web_auth_register(httpd_handle_t server);

/**
 * Check if the request is authenticated. Returns true if auth is disabled or
 * if valid credentials are provided. During AP setup/recovery mode, auth is
 * always enforced using the temporary AP setup password.
 * Call this at the start of protected API handlers.
 */
bool web_auth_check(httpd_req_t *req);

/**
 * Copy the current AP-setup temporary web password into `out`.
 * Returns true when AP recovery auth is active and a password is available.
 */
bool web_auth_get_ap_setup_password(char *out, size_t len);
