//
// Created by Will Castelnau on 2019-06-27.
//
#include "mqtt.h"

#include <freertos/task.h>

#include <cstring>
#include <cJSON.h>

#include <esp_log.h>
#include <esp_rom_md5.h>
#include <thread>

#include "nvram_store.h"
#include "thing_info.h"
#include "events.h"
#include "state.h"

static const char *TAG = "mqtt";

#define NVS_MQTT_NAMESPACE             "giot"
#define MQTT_GIOT_PROJECT_ID           "project_id"
#define MQTT_GIOT_LOCATION             "location_id"
#define MQTT_GIOT_REGISTRY_ID          "registry_id"
#define MQTT_GIOT_CLIENT_PRIVATE_KEY   "private_key"
#define MQTT_GIOT_LAST_CONFIG_MD5      "cfg_md5"

#define MAX_GIOT_FIELD 32

struct mqtt_connect_init_t {
    char project_id[MAX_GIOT_FIELD];
    char location_id[MAX_GIOT_FIELD];
    char registry_id[MAX_GIOT_FIELD];

    char *client_private_key;
};

static mqtt_connect_init_t config = {};
static uint32_t g_mqtt_error_count = 0;
static TickType_t s_last_connect_attempt = 0;
static pthread_mutex_t s_lock = {};

char *subscribe_topic_command, *subscribe_topic_config, *publish_status_topic, *publish_telemetry_topic;

static void (*g_cfg_cb)(const cJSON *) = nullptr;


uint32_t mqtt_get_total_error_count() {
    if (g_mqtt_error_count == 0) {
        // This is our default value
        uint32_t val = 0;
        nvs_handle_t nvs_handle;
        ESP_ERROR_CHECK(nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &nvs_handle));
        ESP_ERROR_CHECK(nvram_store_get_u32(nvs_handle, "mqtt_error_cnt", &val, &val));
        nvs_close(nvs_handle);
        g_mqtt_error_count = val;
    }
    return g_mqtt_error_count;
}

void mqtt_inc_total_error_count() {
    uint32_t count = mqtt_get_total_error_count() + 1;
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvram_store_set_u32(nvs_handle, "mqtt_error_cnt", &count));
    nvs_close(nvs_handle);
    g_mqtt_error_count = count;
}



static void _mqtt_load_settings(const char *nvs_partition, nvs_open_mode_t mode) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open_from_partition(nvs_partition, NVS_MQTT_NAMESPACE, mode, &nvs_handle));

    ESP_ERROR_CHECK(nvram_store_get_str(nvs_handle,
                                        MQTT_GIOT_PROJECT_ID, (char *) &config.project_id, MAX_GIOT_FIELD, ""));
    ESP_ERROR_CHECK(nvram_store_get_str(nvs_handle,
                                        MQTT_GIOT_LOCATION, (char *) &config.location_id, MAX_GIOT_FIELD, ""));
    ESP_ERROR_CHECK(nvram_store_get_str(nvs_handle,
                                        MQTT_GIOT_REGISTRY_ID, (char *) &config.registry_id, MAX_GIOT_FIELD, ""));

    nvram_store_get_blob(nvs_handle, MQTT_GIOT_CLIENT_PRIVATE_KEY, &config.client_private_key);
    nvs_close(nvs_handle);
}


void mqtt_init() {
    config.client_private_key = nullptr;

    _mqtt_load_settings(NVS_DEFAULT_PART_NAME, NVS_READWRITE);

    if (strlen(config.project_id) == 0) {
        ESP_LOGW(TAG, " -- No auth information in nvs, loading from factory nvs "
                CONFIG_HAP_PLATFORM_DEF_NVS_FACTORY_PARTITION);
        _mqtt_load_settings(CONFIG_HAP_PLATFORM_DEF_NVS_FACTORY_PARTITION, NVS_READONLY);

        // Save to current NVS then
        nvs_handle_t nvs_handle;
        ESP_ERROR_CHECK(nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &nvs_handle));
        ESP_ERROR_CHECK(nvram_store_set_str(nvs_handle,
                                            MQTT_GIOT_PROJECT_ID, (char *) &config.project_id));
        ESP_ERROR_CHECK(nvram_store_set_str(nvs_handle,
                                            MQTT_GIOT_LOCATION, (char *) &config.location_id));
        ESP_ERROR_CHECK(nvram_store_set_str(nvs_handle,
                                            MQTT_GIOT_REGISTRY_ID, (char *) &config.registry_id));
        nvram_store_set_blob(nvs_handle, MQTT_GIOT_CLIENT_PRIVATE_KEY, config.client_private_key,
                             strlen(config.client_private_key) + 1);
        nvs_close(nvs_handle);
    } else {
        ESP_LOGI(TAG, " -- Auth information loaded from nvs");
    }

    pthread_mutex_init(&s_lock, NULL);
}


void mqtt_terminate() {


    if (config.client_private_key) {
        free(config.client_private_key);
        config.client_private_key = nullptr;
    }

    pthread_mutex_destroy(&s_lock);
}



bool mqtt_send_status(const char *msg) {
    ESP_LOGD(TAG, "Publishing msg \"%s\" to topic: \"%s\"", msg, publish_status_topic);

    pthread_mutex_lock(&s_lock);


    pthread_mutex_unlock(&s_lock);

    return true;
}

bool mqtt_send_telemetry(const char* msg) {
    ESP_LOGD(TAG, "Publishing msg \"%s\" to topic: \"%s\"", msg, publish_telemetry_topic);

    pthread_mutex_lock(&s_lock);


    pthread_mutex_unlock(&s_lock);

    return true;
}



void mqtt_set_project_id(const char *val) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &nvs_handle));

    strncpy((char *) config.project_id, val, MAX_GIOT_FIELD);
    nvram_store_set_str(nvs_handle, MQTT_GIOT_PROJECT_ID, (const char *) config.project_id);

    nvs_close(nvs_handle);

    // Restart mqtt
}

void mqtt_set_location(const char *val) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &nvs_handle));

    strncpy((char *) config.location_id, val, MAX_GIOT_FIELD);
    nvram_store_set_str(nvs_handle, MQTT_GIOT_LOCATION, (const char *) config.location_id);

    nvs_close(nvs_handle);

    // Restart mqtt
}

void mqtt_set_registry_id(const char *val) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &nvs_handle));

    strncpy((char *) config.registry_id, val, MAX_GIOT_FIELD);
    nvram_store_set_str(nvs_handle, MQTT_GIOT_REGISTRY_ID, (const char *) config.registry_id);

    nvs_close(nvs_handle);

    // Restart mqtt
}

void mqtt_set_client_private_key(const char *val) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_MQTT_NAMESPACE, NVS_READWRITE, &nvs_handle));

    nvram_store_set_blob(nvs_handle, MQTT_GIOT_CLIENT_PRIVATE_KEY, val, strlen(val) + 1);
    nvram_store_get_blob(nvs_handle, MQTT_GIOT_CLIENT_PRIVATE_KEY, &config.client_private_key);

    nvs_close(nvs_handle);

    // Restart mqtt
}

void mqtt_set_cfg_cb(void (*cb)(const cJSON *)) {
    g_cfg_cb = cb;
}
