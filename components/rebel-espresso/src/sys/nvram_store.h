#pragma once

#include <nvs.h>

// Default system namespace storage
#define NVS_NAMESPACE_SYS "sys"

/**
 *  Bring up nvs backend
 */
void nvram_store_init();

esp_err_t nvram_store_get_u64(nvs_handle handle, const char *key, uint64_t *value, void *default_value);
esp_err_t nvram_store_set_u64(nvs_handle handle, const char *key, uint64_t *value);
esp_err_t nvram_store_get_u32(nvs_handle handle, const char *key, uint32_t *value, void *default_value);
esp_err_t nvram_store_set_u32(nvs_handle handle, const char *key, uint32_t *value);
esp_err_t nvram_store_get_u16(nvs_handle handle, const char *key, uint16_t *value, void *default_value);
esp_err_t nvram_store_set_u16(nvs_handle handle, const char *key, uint16_t *value);
esp_err_t nvram_store_get_u8(nvs_handle handle, const char *key, uint8_t *value, void *default_value);
esp_err_t nvram_store_set_u8(nvs_handle handle, const char *key, uint8_t *value);
