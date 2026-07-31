#include "web_api_system.h"

#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_image_format.h>
#include <cJSON.h>
#include <nvs_flash.h>
#include <cstring>

#include "common/identity.h"
#include "app_metrics.h"

#define TAG "api_system"
#define OTA_BUF_SIZE 4096

// --- GET /api/system/info ---

static esp_err_t _info_handler(httpd_req_t *req) {
    const esp_app_desc_t *app = esp_app_get_description();
    auto identity = identity_get();
    auto metrics = app_metrics_get();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "firmware_version", app->version);
    cJSON_AddStringToObject(root, "project_name", app->project_name);
    cJSON_AddStringToObject(root, "idf_version", app->idf_ver);
    cJSON_AddStringToObject(root, "build_date", app->date);
    cJSON_AddStringToObject(root, "build_time", app->time);
    cJSON_AddStringToObject(root, "thing_type", identity->thing_type);
    cJSON_AddStringToObject(root, "thing_id", identity_thing_id());
    cJSON_AddNumberToObject(root, "hardware_rev", identity->hardware_major);
    cJSON_AddNumberToObject(root, "boot_count", metrics.boot_count);
    cJSON_AddNumberToObject(root, "crash_count", metrics.crash_count);
    cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", (double)esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime_sec", (double)(esp_timer_get_time() / 1000000));

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json);

    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

// --- POST /api/system/ota ---

static esp_err_t _ota_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "OTA update started, content length: %d", req->content_len);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition available");
        return ESP_FAIL;
    }

    // Validate size against partition
    if (req->content_len > (int)update_partition->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware too large for OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(OTA_BUF_SIZE);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    bool header_checked = false;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, (remaining < OTA_BUF_SIZE) ? remaining : OTA_BUF_SIZE);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "OTA receive error");
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }

        // Validate first chunk (header)
        if (!header_checked && recv_len >= 48) {
            // Check ESP32 image magic byte
            if ((uint8_t)buf[0] != 0xE9) {
                ESP_LOGE(TAG, "Invalid firmware: bad magic byte 0x%02X", (uint8_t)buf[0]);
                free(buf);
                esp_ota_abort(ota_handle);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware image (bad magic)");
                return ESP_FAIL;
            }

            // Check project name at offset 0x30 (inside esp_app_desc_t)
            // esp_app_desc_t starts at offset 0x20 in the image, project_name is at +0x30 from there
            if (recv_len >= (int)(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))) {
                const esp_app_desc_t *incoming_desc = (const esp_app_desc_t *)(buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
                const esp_app_desc_t *running_desc = esp_app_get_description();

                if (strcmp(incoming_desc->project_name, running_desc->project_name) != 0) {
                    ESP_LOGE(TAG, "Project name mismatch: '%s' vs '%s'", incoming_desc->project_name, running_desc->project_name);
                    free(buf);
                    esp_ota_abort(ota_handle);
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wrong firmware (project name mismatch)");
                    return ESP_FAIL;
                }

                if (strcmp(incoming_desc->version, running_desc->version) == 0) {
                    ESP_LOGW(TAG, "Same version as running firmware: %s", incoming_desc->version);
                    free(buf);
                    esp_ota_abort(ota_handle);
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Same version already running");
                    return ESP_FAIL;
                }

                ESP_LOGI(TAG, "OTA: %s -> %s", running_desc->version, incoming_desc->version);
            }
            header_checked = true;
        }

        err = esp_ota_write(ota_handle, buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= recv_len;
    }

    free(buf);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed (image invalid)");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"OTA complete, rebooting...\"}");

    // Delay briefly to allow response to be sent, then reboot
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK; // unreachable
}

// --- POST /api/system/reboot ---

static esp_err_t _reboot_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Rebooting...\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// --- POST /api/system/factory-reset ---

static esp_err_t _factory_reset_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "Factory reset requested!");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Factory reset, rebooting...\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    nvs_flash_erase();
    esp_restart();
    return ESP_OK;
}

void web_api_system_register(httpd_handle_t server) {
    const httpd_uri_t info_uri = {
        .uri = "/api/system/info",
        .method = HTTP_GET,
        .handler = _info_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &info_uri);

    const httpd_uri_t ota_uri = {
        .uri = "/api/system/ota",
        .method = HTTP_POST,
        .handler = _ota_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &ota_uri);

    const httpd_uri_t reboot_uri = {
        .uri = "/api/system/reboot",
        .method = HTTP_POST,
        .handler = _reboot_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &reboot_uri);

    const httpd_uri_t factory_reset_uri = {
        .uri = "/api/system/factory-reset",
        .method = HTTP_POST,
        .handler = _factory_reset_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &factory_reset_uri);

    ESP_LOGI(TAG, "System API registered");
}
