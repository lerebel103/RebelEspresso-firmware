#include "web_api_status.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <cJSON.h>
#include <cstring>

#include "boiler_temp.h"
#include "brew_temp.h"
#include "boiler_refill.h"
#include "boiler_refill_states.h"
#include "rtds.h"
#include "power.h"
#include "events.h"
#include "measure.h"

#define TAG "api_status"

static esp_err_t _status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    // Temperatures
    measure_t boiler_data = {};
    rtds_get(&boiler_data, RTD_BREW_BOILER_IDX);
    measure_t brew_data = {};
    rtds_get(&brew_data, RTD_BREW_HEAD_IDX);

    cJSON *temps = cJSON_AddObjectToObject(root, "temperatures");
    cJSON_AddNumberToObject(temps, "boiler", boiler_data.value);
    cJSON_AddNumberToObject(temps, "boiler_fault", boiler_data.fault);
    cJSON_AddNumberToObject(temps, "boiler_setpoint", boiler_temp_get_current_setpoint());
    cJSON_AddNumberToObject(temps, "boiler_duty", boiler_temp_get_duty());
    cJSON_AddNumberToObject(temps, "brew_head", brew_data.value);
    cJSON_AddNumberToObject(temps, "brew_head_fault", brew_data.fault);
    cJSON_AddNumberToObject(temps, "brew_head_setpoint", brew_temp_get_setpoint());

    // Power / machine state
    cJSON_AddBoolToObject(root, "power_active", power_is_active());

    // Boiler level
    const char *refill_state = "unknown";
    switch (boiler_refill_state()) {
        case REFILL_STATE_IDLE: refill_state = "ok"; break;
        case REFILL_STATE_ACTIVE: refill_state = "refilling"; break;
        case REFILL_STATE_ERROR: refill_state = "error"; break;
        case REFILL_STATE_STARTING: refill_state = "starting"; break;
        default: break;
    }
    cJSON_AddStringToObject(root, "boiler_level", refill_state);

    // WiFi
    wifi_ap_record_t ap_info = {};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
        cJSON_AddStringToObject(wifi, "ssid", (const char *)ap_info.ssid);
        cJSON_AddNumberToObject(wifi, "rssi", ap_info.rssi);
        cJSON_AddNumberToObject(wifi, "channel", ap_info.primary);
    }

    // System
    cJSON_AddNumberToObject(root, "uptime_sec", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());

    // Send response
    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json);

    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

void web_api_status_register(httpd_handle_t server) {
    const httpd_uri_t uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = _status_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &uri);
    ESP_LOGI(TAG, "Status API registered");
}
