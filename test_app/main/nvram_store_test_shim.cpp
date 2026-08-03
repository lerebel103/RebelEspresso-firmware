/**
 * Test shim for nvram_store.
 *
 * Provides only the get/set functions used by tests and PID code, without
 * the init function that depends on HomeKit partition config.
 */
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "utils/nvram_store.h"

static const char *TAG = "store";

// nvram_store_init() is NOT provided here — tests call nvs_flash_init() directly.

esp_err_t nvram_store_get_u64(nvs_handle handle, const char *key, uint64_t *value, void *default_value) {
    esp_err_t err = nvs_get_u64(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = *(uint64_t *)default_value;
        ESP_LOGD(TAG, "Key %s not found, using default", key);
        err = nvs_set_u64(handle, key, *value);
    }
    return err;
}

esp_err_t nvram_store_set_u64(nvs_handle handle, const char *key, uint64_t *value) {
    esp_err_t err = nvs_set_u64(handle, key, *value);
    nvs_commit(handle);
    return err;
}

esp_err_t nvram_store_get_u32(nvs_handle handle, const char *key, uint32_t *value, void *default_value) {
    esp_err_t err = nvs_get_u32(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = *(uint32_t *)default_value;
        ESP_LOGD(TAG, "Key %s not found, using default", key);
        err = nvs_set_u32(handle, key, *value);
    }
    return err;
}

esp_err_t nvram_store_set_u32(nvs_handle handle, const char *key, uint32_t *value) {
    esp_err_t err = nvs_set_u32(handle, key, *value);
    nvs_commit(handle);
    return err;
}

esp_err_t nvram_store_get_u16(nvs_handle handle, const char *key, uint16_t *value, void *default_value) {
    esp_err_t err = nvs_get_u16(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = *(uint16_t *)default_value;
        ESP_LOGD(TAG, "Key %s not found, using default", key);
        err = nvs_set_u16(handle, key, *value);
    }
    return err;
}

esp_err_t nvram_store_set_u16(nvs_handle handle, const char *key, uint16_t *value) {
    esp_err_t err = nvs_set_u16(handle, key, *value);
    nvs_commit(handle);
    return err;
}

esp_err_t nvram_store_get_u8(nvs_handle handle, const char *key, uint8_t *value, void *default_value) {
    esp_err_t err = nvs_get_u8(handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = *(uint8_t *)default_value;
        ESP_LOGD(TAG, "Key %s not found, using default", key);
        err = nvs_set_u8(handle, key, *value);
    }
    return err;
}

esp_err_t nvram_store_set_u8(nvs_handle handle, const char *key, uint8_t *value) {
    esp_err_t err = nvs_set_u8(handle, key, *value);
    nvs_commit(handle);
    return err;
}
