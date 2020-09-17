#pragma once

/**
 *  Bring up nvs backend
 */
void nvram_store_init();

esp_err_t nvram_store_read_str(const char *key, char *value, size_t max_len, const char* default_value);
esp_err_t nvram_store_write_str(const char *key, const char* value);

esp_err_t nvram_store_read_u32(const char *key, uint32_t *value, uint32_t default_value);
esp_err_t nvram_store_write_u32(const char *key, uint32_t value);
esp_err_t nvram_store_read_i32(const char *key, int32_t *value, int32_t default_value);
esp_err_t nvram_store_write_i32(const char *key, int32_t value);

esp_err_t nvram_store_read_u16(const char *key, uint16_t *value, uint16_t default_value);
esp_err_t nvram_store_write_u16(const char *key, uint16_t value);
esp_err_t nvram_store_read_i16(const char *key, int16_t *value, int16_t default_value);
esp_err_t nvram_store_write_i16(const char *key, int16_t value);

esp_err_t nvram_store_read_u8(const char *key, uint8_t *value, uint8_t default_value);
esp_err_t nvram_store_write_u8(const char *key, uint8_t value);

esp_err_t nvram_store_read_bool(const char *key, bool *value, bool default_value);
esp_err_t nvram_store_write_bool(const char *key, bool value);

void nvram_store_write_blob(const char *key, const char *data, size_t len, char **cache_ptr);
const char* nvram_store_read_blob(const char *key, char **cache_ptr);


uint32_t store_get_cycle_count();
void store_inc_cycle_count();


void store_set_operation_timeout_seconds(uint32_t timeout);
uint32_t store_get_operation_timeout_seconds();






