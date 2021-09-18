#include <esp_err.h>
#include <esp_log.h>
#include <esp_tls.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/ssl.h>
#include <src/thing_info.h>
#include <_generated/version.h>

#include "ota.h"
#include "nvram_store.h"
#include "events.h"
#include "str_utils.h"

#define MAX_OTA_URI 128
#define MAX_VERSION_LEN 32
#define MAX_OTA_API_KEY 128
#define OTA_DEFAULT_TIMEOUT 20000

#define NVS_CFG_STORE "cfg.ota"
#define KEY_OTA_ENABLE "enable"
#define KEY_OTA_URL "url"
#define KEY_OTA_API_KEY "api_key"
#define KEY_OTA_VERSION "version"
#define KEY_OTA_BUILD_TYPE "build_type"

const static char *TAG = "OTA";

extern const uint8_t server_root_cert_pem_start[] asm("_binary_google_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_google_server_root_cert_pem_end");

struct ota_config_t {
    char url[MAX_OTA_URI] = {0};
    char api_key[MAX_OTA_API_KEY] = {0};

    uint32_t timeout_ms = OTA_DEFAULT_TIMEOUT;
    const char *thing_id = nullptr;
    const char *thing_type = nullptr;
    const char *firmware_version = nullptr;
    const char *hardware_revision = nullptr;

    char desiredVersion[MAX_VERSION_LEN] = {0};
    char desiredBuildType[MAX_VERSION_LEN] = {0};
};


static bool s_enabled = false;
static TaskHandle_t g_ota_task_handle = nullptr;
static ota_config_t g_ota_config;
static int g_ota_duration = -1;
static bool s_pending_validate = false;
static uint32_t g_ota_error_count = 0;


void ota_inc_error_count() {
    uint32_t count = ota_get_error_count() + 1;
    esp_err_t err = nvram_store_write_u32("ota_error_cnt", count);
    ESP_ERROR_CHECK(err);
    g_ota_error_count = count;
}

uint32_t ota_get_error_count() {
    if (g_ota_error_count == 0) {
        // This is our default value
        uint32_t val;
        esp_err_t err = nvram_store_read_u32("ota_error_cnt", &val, 0);
        ESP_ERROR_CHECK(err);
        g_ota_error_count = val;
    }
    return g_ota_error_count;
}

void getHost(char *dest) {
    strcpy(dest, strstr((char *) g_ota_config.url, "://") + 3);
    if (dest == nullptr) {
        ESP_LOGE(TAG, "OTA URL appears to be invalid: '%s'", g_ota_config.url);
    }

    char *host_last = strstr(dest, "/");
    if (host_last != nullptr) {
        *host_last = '\0';
    }
}

static void ota_get_latest_version(char *latest_version) {
    // Get host from endpoint, it's already there
    latest_version[0] = '\0';

    char host[MAX_OTA_URI];
    getHost(host);
    if (strlen(host) == 0) {
        ESP_LOGE(TAG, "OTA URL appears to be invalid: '%s'", g_ota_config.url);
        return;
    }

    char *url = new char[256]; // Don't crowd stack
    sprintf(url, "%s/latest-version?thing_type=%s&hardware_revision=%s&build_type=%s",
            g_ota_config.url, g_ota_config.thing_type, g_ota_config.hardware_revision, g_ota_config.desiredBuildType);
    ESP_LOGD(TAG, "OTA version check url='%s' len=%d", url, strlen(url));

    const static int buf_len = 512; // Yes, this is a bit lazy for embedded programming, I hear you
    char *buffer = new char[buf_len];
    sprintf(buffer, "GET %s HTTP/1.0\r\n"
                    "Host: %s\r\n"
                    "User-Agent: %s/1.0 esp32\r\n"
                    "X-DeviceId: %s\r\n"
                    "x-api-key: %s\r\n"
                    "\r\n",
            url, host, g_ota_config.thing_type, g_ota_config.thing_id, g_ota_config.api_key);

    ESP_LOGD(TAG, "Sending query %s", buffer);

    esp_tls_cfg_t cfg = {
            .alpn_protos = nullptr,
            .cacert_pem_buf  = nullptr, //mqtt_conf.ca,
            .cacert_pem_bytes = (unsigned int) 0, //strlen(mqtt_conf.ca),
            .clientcert_pem_buf = nullptr, //(const unsigned char*)mqtt_conf.client_cert,
            .clientcert_pem_bytes = (unsigned int) 0, //strlen(mqtt_conf.client_cert),
            .clientkey_pem_buf = nullptr, //mqtt_conf.client_pk,
            .clientkey_pem_bytes = (unsigned int) 0, //strlen(mqtt_conf.client_pk),
            .clientkey_password = nullptr,
            .clientkey_password_len = 0,
            .non_block = false,
            .use_secure_element = false,
            .timeout_ms = (int) g_ota_config.timeout_ms,
            .use_global_ca_store = true,
            .common_name = nullptr,
            .skip_common_name = false,
            .keep_alive_cfg = nullptr,
            .psk_hint_key = nullptr,
            .crt_bundle_attach = nullptr,
            .ds_data = nullptr
    };

    ESP_LOGI(TAG, "Connecting to '%s'", url);
    esp_tls_t *tls = esp_tls_conn_http_new(url, &cfg);

    if (tls != nullptr) {
        ESP_LOGI(TAG, "Connected to OTA service");
    } else {
        ESP_LOGE(TAG, "Connection failed...");
        esp_tls_conn_delete(tls);
        delete[] buffer;
        delete[] url;
        return;
    }

    if (esp_tls_conn_write(tls, buffer, strlen(buffer)) < 0) {
        ESP_LOGE(TAG, "... socket send failed");
        esp_tls_conn_delete(tls);
        delete[] buffer;
        delete[] url;
        return;
    }

    // Get response back
    bzero(buffer, buf_len);
    int num_read = esp_tls_conn_read(tls, buffer, 15);
    if (num_read > 0 && strstr((char *) buffer, "200 OK") != nullptr) {
        char *body = new char[254];
        bzero(body, 254);

        do {
            bzero(buffer, buf_len);
            num_read = esp_tls_conn_read(tls, buffer, buf_len);

            char *result = strstr(buffer, "\r\n\r\n");
            if (result) {
                result += 4;
                int position = result - buffer;
                int body_length = num_read - position;
                ESP_LOGI(TAG, "body_length=%d, num_read=%d, position=%d", body_length, num_read, position);
                memcpy(body, result, body_length);
                esp_tls_conn_read(tls, &body[body_length], sizeof(body) - body_length);
                break;
            }
        } while (num_read > 0);

        ESP_LOGD(TAG, "Response body is '%s'", body);
        body = strstrip(body);
        strcpy(latest_version, body);
        delete[] body;
    } else {
        ESP_LOGE(TAG, "HTTP status error %s", buffer);
    }

    esp_tls_conn_delete(tls);
    delete[] buffer;
    delete[] url;
}

static bool ota_download_new_firmware(esp_tls_t *tls) {
    esp_err_t err;
    esp_ota_handle_t update_handle = 0;

    const esp_partition_t *update_partition = nullptr;
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG,
                 "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }

    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    update_partition = esp_ota_get_next_update_partition(nullptr);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);
    assert(update_partition != nullptr);

    esp_task_wdt_delete(nullptr);

    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP_ota_begin failed, error=%d", err);
        return false;
    }
    ESP_LOGI(TAG, "esp_ota_begin succeeded");

    /* Read HTTP response */
    int r;
    const static int rec_buf_size = 1024;
    char *recv_buf = new char[rec_buf_size];
    bool body_flag = false;
    int binary_file_length = 0;
    do {
        bzero(recv_buf, rec_buf_size);
        r = esp_tls_conn_read(tls, (unsigned char *) recv_buf, rec_buf_size);

         if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) {
             continue;
         }

        if (!body_flag) {
            // Read until body delimiter
            char *result = strstr(recv_buf, "\r\n\r\n");

            if (result) {
                result += 4;

                int position = result - recv_buf;
                int body_length = r - position;

                err = esp_ota_write(update_handle, (const void *) result, body_length);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%x", err);
                    break;
                }

                binary_file_length += body_length;
                ESP_LOGD(TAG, "Have written image length %d", binary_file_length);

                body_flag = true;
            }
        } else if (r > 0) {
            err = esp_ota_write(update_handle, (const void *) recv_buf, r);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%x", err);
                break;
            }
            binary_file_length += r;
            ESP_LOGD(TAG, "Have written image length %d", binary_file_length);
        }
    } while (r > 0);

    delete[] recv_buf;

    ESP_LOGI(TAG, "Total Write binary data length : %d", binary_file_length);

    if (esp_ota_end(update_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed!");
        return false;
    }

    if (esp_ota_set_boot_partition(update_partition) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed!");
        return false;
    }

    return true;
}

static bool ota_download_firmware(char *version) {
    ESP_LOGI(TAG, "Downloading firmware version %s", version);

    char host[MAX_OTA_URI];
    getHost(host);
    if (strlen(host) == 0) {
        ESP_LOGE(TAG, "OTA URL appears to be invalid: '%s'", g_ota_config.url);
        return false;
    }

    char *url = new char[256]; // Don't crowd stack
    sprintf(url,
            "%s/download?hardware_revision=%s&thing_type=%s&build_type=%s&firmware_version=%s",
            g_ota_config.url, g_ota_config.hardware_revision, g_ota_config.thing_type, g_ota_config.desiredBuildType,
            version);

    const static int buf_len = 1024; // Yes, this is a bit lazy for embedded programming, I hear you
    char *buffer = new char[buf_len];
    sprintf(buffer, "GET %s HTTP/1.0\r\n"
                    "Host: %s\r\n"
                    "User-Agent: %s/1.0 esp32\r\n"
                    "X-DeviceId: %s\r\n"
                    "x-api-key: %s\r\n"
                    "Accept: application/octet-stream\r\n"
                    "\r\n",
            url, host, g_ota_config.thing_type, g_ota_config.thing_id, g_ota_config.api_key);

    esp_tls_cfg_t cfg = {
            .alpn_protos = nullptr,
            .cacert_pem_buf  = nullptr, //mqtt_conf.ca,
            .cacert_pem_bytes = 0, //strlen(mqtt_conf.ca),
            .clientcert_pem_buf = nullptr, //(const unsigned char*)mqtt_conf.client_cert,
            .clientcert_pem_bytes = 0, //strlen(mqtt_conf.client_cert),
            .clientkey_pem_buf = nullptr, //mqtt_conf.client_pk,
            .clientkey_pem_bytes = 0, //strlen(mqtt_conf.client_pk),
            .clientkey_password = nullptr,
            .clientkey_password_len = 0,
            .non_block = false,
            .use_secure_element = false,
            .timeout_ms =(int) g_ota_config.timeout_ms,
            .use_global_ca_store = true,
            .common_name = nullptr,
            .skip_common_name = false,
            .keep_alive_cfg = nullptr,
            .psk_hint_key = nullptr,
            .crt_bundle_attach = nullptr,
            .ds_data = nullptr
    };

    esp_tls_t *tls = esp_tls_conn_http_new(url, &cfg);

    if (tls != nullptr) {
        ESP_LOGI(TAG, "Connected to Firmware download service");
    } else {
        ESP_LOGE(TAG, "Connection failed...");
        esp_tls_conn_delete(tls);
        delete[] buffer;
        delete[] url;
        return false;
    }

    if (esp_tls_conn_write(tls, buffer, strlen(buffer)) < 0) {
        ESP_LOGE(TAG, "... socket send failed");
        esp_tls_conn_delete(tls);
        delete[] buffer;
        delete[] url;
        return false;
    }

    bool is_ok = false;
    bzero(buffer, buf_len);
    esp_tls_conn_read(tls, buffer, 15);

    if (strstr((char *) buffer, "200 OK") != nullptr) {
        is_ok = ota_download_new_firmware(tls);
    } else {
        ESP_LOGE(TAG, "HTTP status code error %s", buffer);
    }

    delete[] buffer;
    delete[] url;
    esp_tls_conn_delete(tls);

    return is_ok;
}

/**
 * Tells us if the firmware version on offer over OTA is newer than ours.
 * We expect <major>.<minor>.<path> as a version string format.
 */
bool ota_can_upgrade(const char *ota_version) {
    bool can_update = false;

    // Parse versions and see if the OTA version is newer.
    unsigned my_major = 0, my_minor = 0, my_patch = 0;
    unsigned ota_major = 0, ota_minor = 0, ota_patch = 0;

    int scan1 = sscanf(g_ota_config.firmware_version, "%u.%u.%u", &my_major, &my_minor, &my_patch);
    int scan2 = sscanf(ota_version, "%u.%u.%u", &ota_major, &ota_minor, &ota_patch);
    if (scan1 == 3 && scan2 == 3) {
        ESP_LOGI(TAG, "Comparing my_version=%u.%u.%u with ota_version=%u.%u.%u",
                 my_major, my_minor, my_patch, ota_major, ota_minor, ota_patch);
        if (my_major < ota_major) {
            can_update = true;
        } else if (my_minor < ota_minor) {
            can_update = true;
        } else if (my_patch < ota_patch) {
            can_update = true;
        }
    } else {
        ESP_LOGE(TAG, "Malformed version received, my_version=%s ota_version=%s", g_ota_config.firmware_version,
                 ota_version);
        ota_inc_error_count();
    }

    return can_update;
}

static void _load_params() {
    if(strlen(g_ota_config.url) == 0) {
        ESP_LOGI(TAG, "Loading OTA params from NVS");
        nvs_handle my_handle;
        ESP_ERROR_CHECK(nvs_open(NVS_CFG_STORE, NVS_READWRITE, &my_handle));

        s_enabled = true;
        nvram_store_get_u8(my_handle, KEY_OTA_ENABLE, (uint8_t*)&s_enabled, (uint8_t*)& s_enabled);
        nvram_store_get_str(my_handle, KEY_OTA_URL, g_ota_config.url, MAX_OTA_URI, g_ota_config.url);
        nvram_store_get_str(my_handle, KEY_OTA_API_KEY, g_ota_config.api_key, MAX_OTA_API_KEY, g_ota_config.api_key);
        nvram_store_get_str(my_handle, KEY_OTA_VERSION, g_ota_config.desiredVersion, MAX_VERSION_LEN,
                            g_ota_config.desiredVersion);
        nvram_store_get_str(my_handle, KEY_OTA_BUILD_TYPE, g_ota_config.desiredBuildType, MAX_VERSION_LEN,
                            g_ota_config.desiredBuildType);

        nvs_close(my_handle);
    }
}


static void do_ota(void *) {
    EventBits_t uxBits = xEventGroupWaitBits(
            status_event_group,
            WIFI_CONNECTED_BIT,
            false, true, 1000 * store_get_operation_timeout_seconds() / portTICK_PERIOD_MS);

    if ((uxBits & WIFI_CONNECTED_BIT) && strlen(g_ota_config.url) > 0) {
        // init global CA store
        if (esp_tls_get_global_ca_store() == nullptr) {
            ESP_LOGI(TAG, "Initialising Global CA store");
            ESP_ERROR_CHECK(esp_tls_init_global_ca_store());
        }

        esp_err_t  esp_ret = esp_tls_set_global_ca_store(server_root_cert_pem_start, server_root_cert_pem_end - server_root_cert_pem_start);
        if (esp_ret != ESP_OK) {
            ESP_LOGE(TAG, "Error in setting the global ca store: [%02X] (%s),could not complete the https_request using global_ca_store", esp_ret, esp_err_to_name(esp_ret));
            if (s_pending_validate) {
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
            goto error;
        }

        TickType_t start_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Get latest firmware available please, or pinned version
        char latest_version[MAX_VERSION_LEN] = {0};
        if (strcmp(g_ota_config.desiredVersion, "latest") == 0) {
            ota_get_latest_version(latest_version);
        } else {
            strcpy(latest_version, g_ota_config.desiredVersion);
        }

        ESP_LOGI(TAG, "Received OTA Firmware version string is: '%s', mine is '%s'",
                 latest_version, g_ota_config.firmware_version);

        if (strlen(latest_version) == 0) {
            ESP_LOGE(TAG, "Could not get latest firmware version.");
            if (s_pending_validate) {
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
            ota_inc_error_count();
        } else if (ota_can_upgrade(latest_version)) {
            // Download firmware - note we don't do anything smart here, just apply whatever version it says
            if (ota_download_firmware(latest_version)) {
                ESP_LOGW(TAG, "Firmware update successful, setting reboot flag.");
                esp_restart();
            } else {
                ESP_LOGE(TAG, "Firmware update failed, ignoring.");
                ota_inc_error_count();
            }
        } else {
            ESP_LOGI(TAG, "No newer firmware available");
        }

        g_ota_duration = xTaskGetTickCount() * portTICK_PERIOD_MS - start_tick;

    } else {
        g_ota_duration = 0;
        ESP_LOGE(TAG, "Wifi or OTA URL not available.");
    }

    error:
    // Tell everyone we are done..
    xEventGroupSetBits(status_event_group, OTA_PERFORMED_BIT);

    ESP_LOGI(TAG, "OTA task finished.");
    g_ota_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void ota_init(
        const char *thing_id,
        const char *thing_type,
        const char *firmware_version,
        const char *hardware_revision) {
    // Load params from nvram
    _load_params();

    g_ota_config.thing_id = thing_id;
    g_ota_config.thing_type = thing_type;
    g_ota_config.firmware_version = firmware_version;
    g_ota_config.hardware_revision = hardware_revision;
}

void ota_update_cfg(const cJSON *config) {
    cJSON *item = config->child;
    while( item ) {
        if ( strend(item->string, OTA_CFG_JSON_KEY "enable") ) {
            if (cJSON_IsBool(item) && item->valueint == 0) {
                s_enabled = false;
                ESP_LOGI(TAG, "OTA disabled");
            } else {
                ESP_LOGI(TAG, "OTA enabled");
                s_enabled = true;
            }
        } else if ( strend(item->string, OTA_CFG_JSON_KEY "url") ) {
            if (cJSON_IsString(item)) {
                strncpy(g_ota_config.url, item->valuestring, MAX_OTA_URI);
            } else {
                ESP_LOGE(TAG, "url is invalid");
                return;
            }
        } else if ( strend(item->string, OTA_CFG_JSON_KEY "api_key") ) {
            if (cJSON_IsString(item)) {
                strncpy(g_ota_config.api_key, item->valuestring, MAX_OTA_URI);
            }
        } else if ( strend(item->string, OTA_CFG_JSON_KEY "desired_version") ) {
            if (cJSON_IsString(item)) {
                strncpy(g_ota_config.desiredVersion, item->valuestring, MAX_VERSION_LEN);
            } else {
                ESP_LOGE(TAG, "desiredVersion is invalid");
                return;
            }
        } else if ( strend(item->string, OTA_CFG_JSON_KEY "desired_build_type") ) {
            if (cJSON_IsString(item)) {
                strncpy(g_ota_config.desiredBuildType, item->valuestring, MAX_VERSION_LEN);
            } else {
                ESP_LOGE(TAG, "desiredBuildType is invalid");
                return;
            }
        }

        item = item->next;
    }

    // save all now
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_CFG_STORE, NVS_READWRITE, &my_handle));

    nvram_store_set_u8(my_handle, KEY_OTA_ENABLE, (uint8_t *) &s_enabled);
    nvram_store_set_str(my_handle, KEY_OTA_URL, g_ota_config.url);
    nvram_store_set_str(my_handle, KEY_OTA_API_KEY, g_ota_config.api_key);
    nvram_store_set_str(my_handle, KEY_OTA_VERSION, g_ota_config.desiredVersion);
    nvram_store_set_str(my_handle, KEY_OTA_BUILD_TYPE, g_ota_config.desiredBuildType);

    nvs_close(my_handle);
}

void ota_cfg_to_json(cJSON* config, const char* base_key) {
    char* buf = (char*)malloc(64);

    sprintf(buf, "%.*s" OTA_CFG_JSON_KEY "enable", 32, base_key);
    cJSON_AddBoolToObject(config, buf, s_enabled);

    sprintf(buf, "%.*s" OTA_CFG_JSON_KEY "url", 32, base_key);
    cJSON_AddStringToObject(config, buf, g_ota_config.url);

    sprintf(buf, "%.*s" OTA_CFG_JSON_KEY "api_key", 32, base_key);
    cJSON_AddStringToObject(config, buf, g_ota_config.api_key);

    sprintf(buf, "%.*s" OTA_CFG_JSON_KEY "desired_version", 32, base_key);
    cJSON_AddStringToObject(config, buf, g_ota_config.desiredVersion);

    sprintf(buf, "%.*s" OTA_CFG_JSON_KEY "desired_build_type", 32, base_key);
    cJSON_AddStringToObject(config, buf, g_ota_config.desiredBuildType);

    free(buf);
}


void ota_check_pending_validate_begin() {
    // Do we need to verify this image first?
    auto partition = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    auto check = esp_ota_get_state_partition(partition, &state);
    if (check == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Verifying new firmware");

            auto* hw_info = thing_info_ext();
            if (strcmp(hw_info->thing_type, THING_TYPE) != 0) {
                ESP_LOGE(TAG, "Firmware is not for this thing type %s vs %s", hw_info->thing_type, THING_TYPE);
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }

            int major_hw_version = 0;
            int minor_hw_version = 0;
            sscanf(HARDWARE_REVISION, "%d.%d", &major_hw_version, &minor_hw_version);
            if (major_hw_version != hw_info->hardware_version_major) {
                ESP_LOGE(TAG, "Firmware is not for this hardware major revision %d vs %d",
                        hw_info->hardware_version_major, major_hw_version);
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }

            // We need validation then
            s_pending_validate = true;
        }
    } else {
        ESP_LOGD(TAG, "Could not get OTA partition state: %d.", check);
    }
}

void ota_check_pending_validate_end() {
    if (s_pending_validate) {
        ESP_LOGD(TAG, "New OTA is good, cancelling rollback");
        esp_ota_mark_app_valid_cancel_rollback();
        s_pending_validate = false;
    }
}

void ota_run() {
    if (g_ota_task_handle != nullptr) {
        ESP_LOGW(TAG, "OTA already in progress");
        return;
    }

    // Good to go! Put it all in a task
    xTaskCreate(do_ota, "ota_run_task", 6 * 1024, nullptr, 2, &g_ota_task_handle);
}

bool ota_is_enabled() {
    return s_enabled;
}

int ota_get_duration() {
    return g_ota_duration;
}

bool ota_is_running() {
    return g_ota_task_handle != nullptr;
}
