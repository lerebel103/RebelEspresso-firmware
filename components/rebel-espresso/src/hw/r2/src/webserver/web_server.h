#pragma once

#include <esp_err.h>

/**
 * Initialize and start the HTTP web server.
 * Registers all API handlers and the embedded SPA static handler.
 * Can be called when WiFi STA connects or when Soft-AP mode activates.
 *
 * @return ESP_OK on success
 */
esp_err_t web_server_start();

/**
 * Stop the HTTP web server.
 */
void web_server_stop();

/**
 * Check if the web server is currently running.
 */
bool web_server_is_running();
