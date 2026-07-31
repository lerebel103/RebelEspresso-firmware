#pragma once

#include <esp_err.h>

/**
 * Initialize and start the HTTP web server.
 * Mounts the FAT data partition, registers all API and static file handlers.
 * Only call this after WiFi is connected (not during AP provisioning).
 *
 * @return ESP_OK on success
 */
esp_err_t web_server_start();

/**
 * Stop the HTTP web server and unmount the data partition.
 */
void web_server_stop();

/**
 * Check if the web server is currently running.
 */
bool web_server_is_running();
