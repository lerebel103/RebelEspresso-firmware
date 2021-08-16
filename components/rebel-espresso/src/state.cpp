#include "state.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <cJSON.h>
#include <sys/ota.h>

#include "thing_info.h"
#include "version.h"
#include "sys/nvram_store.h"
#include "sys/wifi_connect.h"
#include "sys/mqtt.h"
#include "sys/sntp.h"
#include "controller.h"

const char *DIAG_TAG = "state";

state_t g_diagnostics;

void state_print_system_info() {
    ESP_LOGI(DIAG_TAG, "\n\n%s: firwmware v%s, hardware r%s, ID: %s\n\n",
             THING_TYPE, FIRMWARE_VERSION, HARDWARE_REVISION, thing_info_id());
    ESP_LOGI(DIAG_TAG, "Written using ESP-IDF %s", esp_get_idf_version());

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(DIAG_TAG, "This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    ESP_LOGI(DIAG_TAG, "Silicon revision %d, ", chip_info.revision);

    ESP_LOGI(DIAG_TAG, "%dMB %s flash", spi_flash_get_chip_size() / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    ESP_LOGI(DIAG_TAG, "NVS Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)",
             nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);

    state_print_memory_info();
}

void state_print_memory_info() {
    ESP_LOGI(DIAG_TAG, "Memory heap: %d, min: %d", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
}

state_t& state_get() {
    return g_diagnostics;
}

void state_send(time_t timestamp) {
    // Send all over mqtt
    cJSON *root = cJSON_CreateObject();
    cJSON *status = cJSON_AddObjectToObject(root, "status");
    
    cJSON_AddNumberToObject(status, "timestamp", timestamp);
    cJSON_AddStringToObject(status, "thing.id", thing_info_id());
    cJSON_AddStringToObject(status, "thing.type", THING_TYPE);
    cJSON_AddStringToObject(status, "thing.hardware_revision", HARDWARE_REVISION);

    cJSON_AddNumberToObject(status, "sys.uptime", xTaskGetTickCount() * portTICK_PERIOD_MS);
    cJSON_AddNumberToObject(status, "sys.boot_count", store_get_cycle_count());
    cJSON_AddStringToObject(status, "sys.firmware.version", FIRMWARE_VERSION);
    cJSON_AddStringToObject(status, "sys.firmware.git_hash", GIT_SHORT_HASH);
    cJSON_AddStringToObject(status, "sys.firmware.type", BUILD_TYPE);
    cJSON_AddStringToObject(status, "sys.firmware.date", __DATE__);
    cJSON_AddStringToObject(status, "sys.firmware.time", __TIME__);
    cJSON_AddNumberToObject(status, "sys.mem.free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(status, "sys.mem.free_min", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(status, "sys.mem.free_8bit", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(status, "sys.mem.free_32bit", heap_caps_get_largest_free_block(MALLOC_CAP_32BIT));

    cJSON_AddNumberToObject(status, "wifi.rssi", g_diagnostics.wifi_rssi);
    cJSON_AddStringToObject(status, "wifi.bssid", g_diagnostics.wifi_bssid);
    cJSON_AddNumberToObject(status, "wifi.channel", g_diagnostics.wifi_primary_channel);
    cJSON_AddNumberToObject(status, "wifi.join_duration", g_diagnostics.wifi_join_duration);
    cJSON_AddNumberToObject(status, "wifi.connect_error_count", wifi_get_error_count());

    cJSON_AddNumberToObject(status, "mqtt.connect_error_count", mqtt_get_total_error_count());
    cJSON_AddNumberToObject(status, "ota.connect_error_count", ota_get_error_count());
    cJSON_AddNumberToObject(status, "ota_duration", ota_get_duration());
    cJSON_AddNumberToObject(status, "sntp.sync_duration", sntp_sync_get_sync_duration());

    controller_status_to_json(root, "status.");

    controller_cfg_to_json(root, "config.");

    char *json_unformatted = cJSON_Print(root);
    cJSON_Delete(root);
    mqtt_send_status(json_unformatted);
    free(json_unformatted);
}

