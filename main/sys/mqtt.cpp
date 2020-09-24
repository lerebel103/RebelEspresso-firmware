//
// Created by Will Castelnau on 2019-06-27.
//
#include "mqtt.h"

#include <freertos/task.h>

#include <cstring>
#include <cJSON.h>

#include <esp_log.h>
#include <esp32/rom/md5_hash.h>
#include <iotc_types.h>
#include <iotc_connection_data.h>
#include <iotc_tuple.h>
#include <iotc_jwt.h>
#include <iotc.h>

#include "nvram_store.h"
#include "thing_info.h"
#include "events.h"
#include "state.h"

static const char *TAG = "mqtt";

#define MQTT_GIOT_PROJECT_ID           "giot_proj_id"
#define MQTT_GIOT_LOCATION             "giot_loc_id"
#define MQTT_GIOT_REGISTRY_ID          "giot_reg_id"
#define MQTT_GIOT_CLIENT_PRIVATE_KEY   "giot_clt_key"
#define MQTT_GIOT_LAST_CONFIG_MD5      "giot_cfg_md5"

#define MAX_GIOT_FIELD 32

struct mqtt_connect_init_t {
    char project_id[MAX_GIOT_FIELD];
    char location_id[MAX_GIOT_FIELD];
    char registry_id[MAX_GIOT_FIELD];

    char *client_private_key;
};

#define DEVICE_PATH "projects/%s/locations/%s/registries/%s/devices/%s"
#define SUBSCRIBE_TOPIC_COMMAND "/devices/%s/commands/#"
#define SUBSCRIBE_TOPIC_CONFIG "/devices/%s/config"
#define PUBLISH_TOPIC_STATE "/devices/%s/state"

#define PUBLISH_TOPIC_EVENT_TEMPERATURE "/devices/%s/events/temperature"
#define PUBLISH_TOPIC_EVENT_FAN "/devices/%s/events/fan"

static mqtt_connect_init_t config;
static uint32_t g_mqtt_error_count = 0;
static TickType_t s_last_connect_attempt = 0;

char *subscribe_topic_command, *subscribe_topic_config;

static void (*g_ota_cfg_cb)(const cJSON *) = nullptr;
static void (*g_temperature_cfg_cb)(const cJSON *) = nullptr;
static void (*g_fan_cfg_cb)(const cJSON *) = nullptr;
static void (*g_controller_cfg_cb)(const cJSON *) = nullptr;

void mqtt_reconnect(iotc_context_handle_t in_context_handle, const iotc_connection_data_t *conn_data);

static iotc_context_handle_t iotc_context = IOTC_INVALID_CONTEXT_HANDLE;

uint32_t mqtt_get_total_error_count() {
    if (g_mqtt_error_count == 0) {
        // This is our default value
        uint32_t val;
        esp_err_t err = nvram_store_read_u32("mqtt_error_cnt", &val, 0);
        ESP_ERROR_CHECK(err);
        g_mqtt_error_count = val;
    }
    return g_mqtt_error_count;
}

void mqtt_inc_total_error_count() {
    uint32_t count = mqtt_get_total_error_count() + 1;
    esp_err_t err = nvram_store_write_u32("mqtt_error_cnt", count);
    ESP_ERROR_CHECK(err);
    g_mqtt_error_count = count;
}

TickType_t mqtt_last_connect_attempt() {
    return s_last_connect_attempt;
}

static char *process_message(
        iotc_context_handle_t in_context_handle, iotc_sub_call_type_t call_type,
        const iotc_sub_call_params_t *const params, iotc_state_t state,
        void *user_data) {
    IOTC_UNUSED(in_context_handle);
    IOTC_UNUSED(call_type);
    IOTC_UNUSED(state);
    IOTC_UNUSED(user_data);
    if (params != nullptr && params->message.topic != nullptr) {
        ESP_LOGD(TAG, "Inbound msg on '%s'", params->message.topic);
        char *sub_message = (char *) malloc(params->message.temporary_payload_data_length + 1);
        if (sub_message == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate memory");
            return nullptr;
        }
        memcpy(sub_message, params->message.temporary_payload_data, params->message.temporary_payload_data_length);
        sub_message[params->message.temporary_payload_data_length] = '\0';
        ESP_LOGD(TAG, "Payload: %s ", sub_message);

        return sub_message;
    }

    return nullptr;
}


static void config_cb(
        iotc_context_handle_t in_context_handle, iotc_sub_call_type_t call_type,
        const iotc_sub_call_params_t *const params, iotc_state_t state,
        void *user_data) {
    char *msg = process_message(in_context_handle, call_type, params, state, user_data);

    if (strlen(msg) > 0) {
        cJSON *root = cJSON_Parse(msg);
        if (root) {
            // Parse payload as json and send to the relevant bits of the system, ota always,
            // this is a bit of a hack really, we need to treat this as conf rather than a trigger for OTA
            cJSON *sub_system = cJSON_GetObjectItem(root, "ota");
            if (cJSON_IsObject(sub_system) && g_ota_cfg_cb) {
                g_ota_cfg_cb(sub_system);
            }

            // If md5 is the same as what was previously processed, then no need to take it in again.
            MD5Context ctx;
            MD5Init(&ctx);
            MD5Update(&ctx, (uint8_t *) (msg), strlen(msg));
            uint8_t digest[16] = {0};
            MD5Final(digest, &ctx);
            char md5[17];
            strncpy(md5, (char *) digest, 16);
            md5[16] = '\0';

            char last_md5[17];
            nvram_store_read_str(MQTT_GIOT_LAST_CONFIG_MD5, last_md5, 17, "");
            if (strcmp(last_md5, md5) == 0) {
                ESP_LOGI(TAG, "No config update, MD5 is identical to last processed");
            } else {
                sub_system = cJSON_GetObjectItem(root, "temperature");
                if (cJSON_IsObject(sub_system) && g_temperature_cfg_cb) {
                    g_temperature_cfg_cb(sub_system);
                }
                sub_system = cJSON_GetObjectItem(root, "fan");
                if (cJSON_IsObject(sub_system) && g_fan_cfg_cb) {
                    g_fan_cfg_cb(sub_system);
                }
                sub_system = cJSON_GetObjectItem(root, "controller");
                if (cJSON_IsObject(sub_system) && g_controller_cfg_cb) {
                    g_controller_cfg_cb(sub_system);
                }

                // store MD5
                nvram_store_write_str(MQTT_GIOT_LAST_CONFIG_MD5, md5);
            }
        }
        cJSON_Delete(root);
    }

    free(msg);
}

static void command_cb(
        iotc_context_handle_t in_context_handle, iotc_sub_call_type_t call_type,
        const iotc_sub_call_params_t *const params, iotc_state_t state,
        void *user_data) {
    char *msg = process_message(in_context_handle, call_type, params, state, user_data);

    free(msg);
}

static iotc_state_t new_jwt(char *jwt) {
    /* Format the key type descriptors so the client understands
     which type of key is being represented. In this case, a PEM encoded
     byte array of a ES256 key. */
    iotc_crypto_key_data_t iotc_connect_private_key_data;
    iotc_connect_private_key_data.crypto_key_signature_algorithm = IOTC_CRYPTO_KEY_SIGNATURE_ALGORITHM_ES256;
    iotc_connect_private_key_data.crypto_key_union_type = IOTC_CRYPTO_KEY_UNION_TYPE_PEM;
    iotc_connect_private_key_data.crypto_key_union.key_pem.key = (char *) config.client_private_key;

    // Wait for time sync before we can start to build JWT tokens
    xEventGroupWaitBits(status_event_group, TIME_SYNC_BIT, false, true, portMAX_DELAY);

    /* Generate the client authentication JWT, which will serve as the MQTT
     * password. */
    /*60*60*24*/
    size_t bytes_written = 0;
    return iotc_create_iotcore_jwt(
            config.project_id,
            /*jwt_expiration_period_sec=*/ 60 * 60 * 24, &iotc_connect_private_key_data, jwt,
            IOTC_JWT_SIZE, &bytes_written);

}

void on_connection_state_changed(iotc_context_handle_t in_context_handle,
                                 void *data, iotc_state_t state) {
    iotc_connection_data_t *conn_data = (iotc_connection_data_t *) data;

    switch (conn_data->connection_state) {
        case IOTC_CONNECTION_STATE_OPENED:
            ESP_LOGI(TAG, "connected!");

            asprintf(&subscribe_topic_command, SUBSCRIBE_TOPIC_COMMAND, thing_info_id());
            ESP_LOGI(TAG, "Subscribe to topic: \"%s\"", subscribe_topic_command);
            iotc_subscribe(in_context_handle, subscribe_topic_command, IOTC_MQTT_QOS_AT_LEAST_ONCE,
                           &command_cb, /*user_data=*/nullptr);

            asprintf(&subscribe_topic_config, SUBSCRIBE_TOPIC_CONFIG, thing_info_id());
            ESP_LOGI(TAG, "Subscribe to topic: \"%s\"", subscribe_topic_config);
            iotc_subscribe(in_context_handle, subscribe_topic_config, IOTC_MQTT_QOS_AT_LEAST_ONCE,
                           &config_cb, /*user_data=*/nullptr);


            xEventGroupSetBits(status_event_group, MQTT_CONNECTED_BIT);
            s_last_connect_attempt = xTaskGetTickCount() * portTICK_PERIOD_MS;
            break;

        case IOTC_CONNECTION_STATE_OPEN_FAILED:
            mqtt_inc_total_error_count();
            ESP_LOGI(TAG, "ERROR!\tConnection has failed reason %d", state);

            mqtt_reconnect(in_context_handle, conn_data);
            xEventGroupClearBits(status_event_group, MQTT_CONNECTED_BIT);
            break;

        case IOTC_CONNECTION_STATE_CLOSED:
            free(subscribe_topic_command);
            free(subscribe_topic_config);
            /* When the connection is closed it's better to cancel some of previously
               registered activities. Using cancel function on handler will remove the
               handler from the timed queue which prevents the registered handle to be
               called when there is no connection. */

            if (state == IOTC_STATE_OK) {
                /* The connection has been closed intentionally. Therefore, stop
                   the event processing loop as there's nothing left to do
                   in this example. */
                iotc_events_stop();
            } else {
                ESP_LOGW(TAG, "Connection closed - reason %d!", state);
                /* The disconnection was unforeseen.  Try reconnect to the server
                with previously set configuration, which has been provided
                to this callback in the conn_data structure. */
                mqtt_reconnect(in_context_handle, conn_data);
            }

            // Clear MQTT connection status
            s_last_connect_attempt = xTaskGetTickCount() * portTICK_PERIOD_MS;
            xEventGroupClearBits(status_event_group, MQTT_CONNECTED_BIT);
            break;

        default:
            ESP_LOGI(TAG, "wrong value");
            break;
    }
}

void mqtt_reconnect(iotc_context_handle_t in_context_handle, const iotc_connection_data_t *conn_data) {
    char jwt[IOTC_JWT_SIZE] = {0};
    iotc_state_t state = new_jwt(jwt);
    if (IOTC_STATE_OK != state) {
        ESP_LOGE(TAG, "iotc_create_iotcore_jwt returned with error: %ul", state);
        mqtt_inc_total_error_count();
    } else {
        state_print_memory_info();
        iotc_shutdown_connection(in_context_handle);
        state_print_memory_info();
        ESP_LOGW(TAG, "re-connecting");
        iotc_connect(
                in_context_handle, conn_data->username, jwt, conn_data->client_id,
                conn_data->connection_timeout, conn_data->keepalive_timeout,
                &on_connection_state_changed);
    }
}


static void mqtt_task(void *pvParameters) {
    /* initialize iotc library and create a context to use to connect to the
    * GCP IoT Core Service. */
    iotc_state_t error_init = iotc_initialize();

    // That's all you can take, forget it gobbling up all my heap damn it!
    iotc_set_maximum_heap_usage(58 * 1024);

    if (IOTC_STATE_OK != error_init) {
        ESP_LOGE(TAG, "iotc failed to initialize, error: %d", error_init);
        vTaskDelete(nullptr);
    }

    /*  Create a connection context. A context represents a Connection
        on a single socket, and can be used to publish and subscribe
        to numerous topics. */
    iotc_context = iotc_create_context();
    if (IOTC_INVALID_CONTEXT_HANDLE >= iotc_context) {
        ESP_LOGI(TAG, " iotc failed to create context, error: %d", -iotc_context);
        vTaskDelete(nullptr);
    }

    /*  Queue a connection request to be completed asynchronously.
        The 'on_connection_state_changed' parameter is the name of the
        callback function after the connection request completes, and its
        implementation should handle both successful connections and
        unsuccessful connections as well as disconnections. */
    const uint16_t connection_timeout = 10;
    const uint16_t keepalive_timeout = 60;

    char jwt[IOTC_JWT_SIZE] = {0};
    iotc_state_t state = new_jwt(jwt);
    if (IOTC_STATE_OK != state) {
        ESP_LOGE(TAG, "iotc_create_iotcore_jwt returned with error: %ul", state);
        vTaskDelete(nullptr);
    }

    char *device_path = nullptr;
    asprintf(&device_path, DEVICE_PATH, config.project_id, config.location_id, config.registry_id, thing_info_id());

    iotc_connect(iotc_context, nullptr, jwt, device_path, connection_timeout,
                 keepalive_timeout, &on_connection_state_changed);
    free(device_path);

    iotc_events_process_blocking();

    iotc_delete_context(iotc_context);

    iotc_shutdown();

    vTaskDelete(nullptr);
}


void mqtt_init() {
    ESP_ERROR_CHECK(nvram_store_read_str(
            MQTT_GIOT_PROJECT_ID, (char *) &config.project_id, MAX_GIOT_FIELD, ""));
    ESP_ERROR_CHECK(nvram_store_read_str(
            MQTT_GIOT_LOCATION, (char *) &config.location_id, MAX_GIOT_FIELD, ""));
    ESP_ERROR_CHECK(nvram_store_read_str(
            MQTT_GIOT_REGISTRY_ID, (char *) &config.registry_id, MAX_GIOT_FIELD, ""));

    nvram_store_read_blob(MQTT_GIOT_CLIENT_PRIVATE_KEY, &config.client_private_key);

    xTaskCreate(&mqtt_task, "mqtt_task", 8192, nullptr, 5, nullptr);
}


bool mqtt_send_status(const char *msg) {
    char *publish_topic = nullptr;
    asprintf(&publish_topic, PUBLISH_TOPIC_STATE, thing_info_id());

    ESP_LOGD(TAG, "Publishing msg \"%s\" to topic: \"%s\"", msg, publish_topic);

    iotc_publish(iotc_context, publish_topic, msg,
                 IOTC_MQTT_QOS_AT_MOST_ONCE,
            /*callback=*/nullptr, /*user_data=*/nullptr);
    free(publish_topic);

    return true;
}

bool mqtt_send_temperature(const char *msg) {
    // Very particular about this, cannot be sent in less than 1 seconds intervals.
    // If we do, IoT core kills our connection.
    if(xEventGroupGetBits(status_event_group) & MQTT_CONNECTED_BIT) {
        char *publish_topic = nullptr;
        asprintf(&publish_topic, PUBLISH_TOPIC_EVENT_TEMPERATURE, thing_info_id());

        ESP_LOGD(TAG, "Publishing msg \"%s\" to topic: \"%s\"", msg, publish_topic);

        iotc_publish(iotc_context, publish_topic, msg,
                     IOTC_MQTT_QOS_AT_MOST_ONCE,
                /*callback=*/nullptr, /*user_data=*/nullptr);
        free(publish_topic);

        return true;
    } else {
        return false;
    }
}

bool mqtt_send_fan(const char *msg) {
    // Very particular about this, cannot be sent in less than 1 seconds intervals.
    // If we do, IoT core kills our connection.
    if(xEventGroupGetBits(status_event_group) & MQTT_CONNECTED_BIT) {
        char *publish_topic = nullptr;
        asprintf(&publish_topic, PUBLISH_TOPIC_EVENT_FAN, thing_info_id());

        ESP_LOGD(TAG, "Publishing msg \"%s\" to topic: \"%s\"", msg, publish_topic);

        iotc_publish(iotc_context, publish_topic, msg,
                     IOTC_MQTT_QOS_AT_MOST_ONCE,
                /*callback=*/nullptr, /*user_data=*/nullptr);
        free(publish_topic);

        return true;
    } else {
        return false;
    }
}


void mqtt_set_project_id(const char *val) {
    strncpy((char *) config.project_id, val, MAX_GIOT_FIELD);
    nvram_store_write_str(MQTT_GIOT_PROJECT_ID, (const char *) config.project_id);

    // Restart mqtt
}

void mqtt_set_location(const char *val) {
    strncpy((char *) config.location_id, val, MAX_GIOT_FIELD);
    nvram_store_write_str(MQTT_GIOT_LOCATION, (const char *) config.location_id);

    // Restart mqtt
}

void mqtt_set_registry_id(const char *val) {
    strncpy((char *) config.registry_id, val, MAX_GIOT_FIELD);
    nvram_store_write_str(MQTT_GIOT_REGISTRY_ID, (const char *) config.registry_id);

    // Restart mqtt
}

void mqtt_set_client_private_key(const char *val) {
    nvram_store_write_blob(MQTT_GIOT_CLIENT_PRIVATE_KEY, val, strlen(val) + 1, &config.client_private_key);
    nvram_store_read_blob(MQTT_GIOT_CLIENT_PRIVATE_KEY, &config.client_private_key);

    // Restart mqtt
}

void mqtt_set_ota_cfg_cb(void (*cb)(const cJSON *)) {
    g_ota_cfg_cb = cb;
}

void mqtt_set_temperature_cfg_cb(void (*cb)(const cJSON *)) {
    g_temperature_cfg_cb = cb;
}

void mqtt_set_fan_cfg_cb(void (*cb)(const cJSON*)) {
    g_fan_cfg_cb = cb;
}

void mqtt_set_controller_cfg_cb(void (*cb)(const cJSON *)) {
    g_controller_cfg_cb = cb;
}
