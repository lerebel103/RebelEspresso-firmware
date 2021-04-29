#pragma once

#include <nvs.h>

// Default system namespace storage
#define NVS_NAMESPACE_SYS "sys"


/**
 *  Bring up nvs backend
 */
void nvram_store_init();

esp_err_t nvram_store_get_str(nvs_handle handle, const char *key, char *value, size_t max_len, const char* default_value);
esp_err_t nvram_store_set_str(nvs_handle handle, const char *key, const char* value);
esp_err_t nvram_store_read_str(const char *key, char *value, size_t max_len, const char* default_value);
esp_err_t nvram_store_write_str(const char *key, const char* value);

esp_err_t nvram_store_get_u64(nvs_handle handle, const char *key, uint64_t *value, void* default_value);
esp_err_t nvram_store_set_u64(nvs_handle handle, const char *key, uint64_t* value);
esp_err_t nvram_store_get_u32(nvs_handle handle, const char *key, uint32_t *value, void* default_value);
esp_err_t nvram_store_set_u32(nvs_handle handle, const char *key, uint32_t* value);
esp_err_t nvram_store_get_u16(nvs_handle handle, const char *key, uint16_t *value, void* default_value);
esp_err_t nvram_store_set_u16(nvs_handle handle, const char *key, uint16_t* value);
esp_err_t nvram_store_get_u8(nvs_handle handle, const char *key, uint8_t *value, void* default_value);
esp_err_t nvram_store_set_u8(nvs_handle handle, const char *key, uint8_t* value);


esp_err_t nvram_store_get_i32(nvs_handle handle, const char *key, int32_t *value, void* default_value);
esp_err_t nvram_store_set_i32(nvs_handle handle, const char *key, int32_t* value);

void nvram_store_set_blob(nvs_handle handle, const char *key, const char *data, size_t len);
const char* nvram_store_get_blob(nvs_handle handle, const char *key, char **cache_ptr);


/* Legacy APIs */

esp_err_t nvram_store_read_u32(const char *key, uint32_t *value, uint32_t default_value);
esp_err_t nvram_store_write_u32(const char *key, uint32_t value);

/* -- end of Legacy APIs */

uint32_t store_get_cycle_count();
void store_inc_cycle_count();


void store_set_operation_timeout_seconds(uint32_t timeout);
uint32_t store_get_operation_timeout_seconds();






