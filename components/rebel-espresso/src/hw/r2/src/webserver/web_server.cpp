#include "web_server.h"

#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_vfs_fat.h>
#include <cstring>

#include "web_static.h"
#include "web_api_status.h"
#include "web_api_config.h"
#include "web_api_system.h"
#include "web_auth.h"

#define TAG "webserver"

#define DATA_PARTITION_LABEL "data"
#define DATA_MOUNT_POINT "/data"
#define MAX_CONNECTIONS 5

static httpd_handle_t s_server = nullptr;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

static bool s_running = false;

static esp_err_t _mount_data_partition() {
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 8,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        DATA_MOUNT_POINT,
        DATA_PARTITION_LABEL,
        &mount_config,
        &s_wl_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FAT partition '%s': %s",
                 DATA_PARTITION_LABEL, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "FAT partition '%s' mounted at %s", DATA_PARTITION_LABEL, DATA_MOUNT_POINT);
    return ESP_OK;
}

static void _unmount_data_partition() {
    if (s_wl_handle != WL_INVALID_HANDLE) {
        esp_vfs_fat_spiflash_unmount_rw_wl(DATA_MOUNT_POINT, s_wl_handle);
        s_wl_handle = WL_INVALID_HANDLE;
        ESP_LOGI(TAG, "FAT partition unmounted");
    }
}

esp_err_t web_server_start() {
    if (s_running) {
        ESP_LOGW(TAG, "Web server already running");
        return ESP_OK;
    }

    // Mount the data partition for static file serving
    esp_err_t err = _mount_data_partition();
    if (err != ESP_OK) {
        return err;
    }

    // Configure and start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;  // Port 80 is used by HomeKit
    config.max_uri_handlers = 20;
    config.max_open_sockets = MAX_CONNECTIONS;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        _unmount_data_partition();
        return err;
    }

    // Register API handlers
    web_api_status_register(s_server);
    web_api_config_register(s_server);
    web_api_system_register(s_server);
    web_auth_register(s_server);

    // Register static file handler last (wildcard catch-all)
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

    _unmount_data_partition();
    s_running = false;
    ESP_LOGI(TAG, "Web server stopped");
}

bool web_server_is_running() {
    return s_running;
}
