#include "web_static.h"

#include <esp_log.h>
#include <cstring>
#include <sys/stat.h>

#define TAG "web_static"
#define DATA_MOUNT_POINT "/data"
#define CHUNK_SIZE 1024

/**
 * Determine MIME type from file extension.
 */
static const char *_get_mime_type(const char *path) {
    if (strstr(path, ".htm")) return "text/html";
    if (strstr(path, ".js")) return "application/javascript";
    if (strstr(path, ".css")) return "text/css";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".png")) return "image/png";
    if (strstr(path, ".ico")) return "image/x-icon";
    if (strstr(path, ".svg")) return "image/svg+xml";
    return "application/octet-stream";
}

/**
 * Serve a file from the data partition.
 * Prefers .gz version if available (serves with Content-Encoding: gzip).
 */
static esp_err_t _static_handler(httpd_req_t *req) {
    char filepath[128];
    const char *uri = req->uri;

    // Strip query string if present
    const char *query = strchr(uri, '?');
    size_t uri_len = query ? (size_t)(query - uri) : strlen(uri);

    // Default to index.htm for root or paths without extension
    if (uri_len == 1 && uri[0] == '/') {
        snprintf(filepath, sizeof(filepath), "%s/index.htm", DATA_MOUNT_POINT);
    } else {
        snprintf(filepath, sizeof(filepath), "%s%.*s", DATA_MOUNT_POINT, (int)uri_len, uri);
    }

    // Try gzipped version first (.gz suffix)
    char gz_path[140];
    const char *serve_path = filepath;
    bool is_gzip = false;

    // For index.htm, try index.gz
    if (strstr(filepath, "index.htm")) {
        snprintf(gz_path, sizeof(gz_path), "%s/index.gz", DATA_MOUNT_POINT);
    } else {
        snprintf(gz_path, sizeof(gz_path), "%s.gz", filepath);
    }

    struct stat st;
    if (stat(gz_path, &st) == 0) {
        serve_path = gz_path;
        is_gzip = true;
    }

    // Open file
    FILE *f = fopen(serve_path, "rb");
    if (!f) {
        // Try index.htm for SPA routing (any path without extension)
        if (!strchr(uri + 1, '.')) {
            snprintf(filepath, sizeof(filepath), "%s/index.htm", DATA_MOUNT_POINT);
            snprintf(gz_path, sizeof(gz_path), "%s/index.gz", DATA_MOUNT_POINT);
            struct stat st2;
            if (stat(gz_path, &st2) == 0) {
                serve_path = gz_path;
                is_gzip = true;
            } else {
                serve_path = filepath;
                is_gzip = false;
            }
            f = fopen(serve_path, "rb");
        }

        if (!f) {
            ESP_LOGD(TAG, "File not found: %s", filepath);
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
            return ESP_FAIL;
        }
    }

    // Set headers
    httpd_resp_set_type(req, _get_mime_type(filepath));
    if (is_gzip) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");

    // Stream file
    char buf[CHUNK_SIZE];
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, CHUNK_SIZE, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
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
    ESP_LOGI(TAG, "Static file handler registered (mount: %s)", DATA_MOUNT_POINT);
}
