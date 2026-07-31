#include "web_auth.h"

#include <esp_log.h>
#include <nvs.h>
#include <mbedtls/md.h>
#include <cJSON.h>
#include <cstring>

#define TAG "web_auth"
#define NVS_NAMESPACE "sys"
#define NVS_KEY_AUTH_ENABLED "http_auth_en"
#define NVS_KEY_AUTH_HASH "http_auth_hash"
#define SHA256_HEX_LEN 64

static bool s_auth_enabled = false;
static char s_password_hash[SHA256_HEX_LEN + 1] = {};

static void _load_auth_state() {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    uint8_t enabled = 0;
    nvs_get_u8(handle, NVS_KEY_AUTH_ENABLED, &enabled);
    s_auth_enabled = (enabled != 0);

    if (s_auth_enabled) {
        size_t len = sizeof(s_password_hash);
        nvs_get_str(handle, NVS_KEY_AUTH_HASH, s_password_hash, &len);
    }

    nvs_close(handle);
}

static void _sha256_hex(const char *input, char *output) {
    unsigned char hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char *)input, strlen(input));
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    for (int i = 0; i < 32; i++) {
        sprintf(output + i * 2, "%02x", hash[i]);
    }
    output[64] = '\0';
}

/**
 * Decode Base64 for Basic Auth header. Simple inline decoder for small inputs.
 */
static int _base64_decode(const char *in, size_t in_len, char *out, size_t out_max) {
    static const unsigned char d[] = {
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,62,64,64,64,63,52,53,54,55,56,57,58,59,60,61,64,64,64,64,64,64,
        64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,64,64,64,64,64,
        64,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,64,64,64,64,64
    };

    size_t out_len = 0;
    unsigned int buf = 0;
    int bits = 0;

    for (size_t i = 0; i < in_len && in[i] != '='; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c >= 128 || d[c] == 64) continue;
        buf = (buf << 6) | d[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out_len < out_max - 1) {
                out[out_len++] = (char)((buf >> bits) & 0xFF);
            }
        }
    }
    out[out_len] = '\0';
    return (int)out_len;
}

bool web_auth_check(httpd_req_t *req) {
    if (!s_auth_enabled) {
        return true;
    }

    char auth_header[128] = {};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"RebelEspresso\"");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return false;
    }

    // Parse "Basic <base64>"
    if (strncmp(auth_header, "Basic ", 6) != 0) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid auth scheme");
        return false;
    }

    char decoded[128] = {};
    _base64_decode(auth_header + 6, strlen(auth_header + 6), decoded, sizeof(decoded));

    // Format is "user:password" — we only check the password part
    char *colon = strchr(decoded, ':');
    const char *password = colon ? colon + 1 : decoded;

    char hash[SHA256_HEX_LEN + 1];
    _sha256_hex(password, hash);

    if (strcmp(hash, s_password_hash) != 0) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"RebelEspresso\"");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
        return false;
    }

    return true;
}

// --- GET /api/auth ---

static esp_err_t _auth_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", s_auth_enabled);

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json);

    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

// --- POST /api/auth (set password) ---

static esp_err_t _auth_post_handler(httpd_req_t *req) {
    char body[256];
    int len = req->content_len;
    if (len <= 0 || len >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }
    httpd_req_recv(req, body, len);
    body[len] = '\0';

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *pw = cJSON_GetObjectItem(json, "password");
    if (!pw || !cJSON_IsString(pw) || strlen(pw->valuestring) < 4) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password must be at least 4 characters");
        return ESP_FAIL;
    }

    // Hash and store
    _sha256_hex(pw->valuestring, s_password_hash);
    s_auth_enabled = true;
    cJSON_Delete(json);

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    uint8_t enabled = 1;
    nvs_set_u8(handle, NVS_KEY_AUTH_ENABLED, enabled);
    nvs_set_str(handle, NVS_KEY_AUTH_HASH, s_password_hash);
    nvs_commit(handle);
    nvs_close(handle);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Password set, auth enabled\"}");
    return ESP_OK;
}

// --- DELETE /api/auth (disable auth) ---

static esp_err_t _auth_delete_handler(httpd_req_t *req) {
    // Must be authenticated to disable
    if (!web_auth_check(req)) {
        return ESP_FAIL;
    }

    s_auth_enabled = false;
    memset(s_password_hash, 0, sizeof(s_password_hash));

    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    uint8_t enabled = 0;
    nvs_set_u8(handle, NVS_KEY_AUTH_ENABLED, enabled);
    nvs_erase_key(handle, NVS_KEY_AUTH_HASH);
    nvs_commit(handle);
    nvs_close(handle);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Auth disabled\"}");
    return ESP_OK;
}

void web_auth_register(httpd_handle_t server) {
    _load_auth_state();

    const httpd_uri_t get_uri = {
        .uri = "/api/auth",
        .method = HTTP_GET,
        .handler = _auth_get_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &get_uri);

    const httpd_uri_t post_uri = {
        .uri = "/api/auth",
        .method = HTTP_POST,
        .handler = _auth_post_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &post_uri);

    const httpd_uri_t delete_uri = {
        .uri = "/api/auth",
        .method = HTTP_DELETE,
        .handler = _auth_delete_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &delete_uri);

    ESP_LOGI(TAG, "Auth API registered (auth %s)", s_auth_enabled ? "enabled" : "disabled");
}
