#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <malloc.h>


#include "nvram_store.h"

const static char *TAG = "store";

uint32_t g_cycle_count = 0;

void nvram_store_init() {
  // We first setup our non-volatile storage
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  ESP_LOGI(TAG, "NVS initialised.");

  // Also init factory partition
  ESP_ERROR_CHECK(nvs_flash_init_partition(CONFIG_HAP_PLATFORM_DEF_NVS_FACTORY_PARTITION));
}

esp_err_t nvram_store_read_u32(const char *key, uint32_t *value, uint32_t default_value) {
  nvs_handle my_handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE_SYS, NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    ESP_ERROR_CHECK(err);
  } else {
    // Read
    err = nvram_store_get_u32(my_handle, key, value, &default_value);
  }
  nvs_close(my_handle);
  return err;
}

esp_err_t nvram_store_get_u32(nvs_handle my_handle, const char *key, uint32_t *value, void *default_value) {
  esp_err_t err = nvs_get_u32(my_handle, key, value);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Then set it
    *value = *(uint32_t *) default_value;
    ESP_LOGI(TAG, "Key %s not found, setting to default %" PRIu32, key, *value);
    err = nvs_set_u32(my_handle, key, *value);
  }
  return err;
}

esp_err_t nvram_store_set_u32(nvs_handle my_handle, const char *key, uint32_t *value) {
  esp_err_t err;
  err = nvs_set_u32(my_handle, key, *value);
  nvs_commit(my_handle);
  return err;
}

esp_err_t nvram_store_get_u64(nvs_handle my_handle, const char *key, uint64_t *value, void *default_value) {
  esp_err_t err = nvs_get_u64(my_handle, key, value);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Then set it
    *value = *(uint64_t *) default_value;
    ESP_LOGI(TAG, "Key %s not found, setting to default %llu", key, *value);
    err = nvs_set_u64(my_handle, key, *value);
  }
  return err;
}

esp_err_t nvram_store_set_u64(nvs_handle my_handle, const char *key, uint64_t *value) {
  esp_err_t err;
  err = nvs_set_u64(my_handle, key, *value);
  nvs_commit(my_handle);
  return err;
}

esp_err_t nvram_store_get_u16(nvs_handle my_handle, const char *key, uint16_t *value, void *default_value) {
  esp_err_t err = nvs_get_u16(my_handle, key, value);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Then set it
    *value = *(uint16_t *) default_value;
    ESP_LOGI(TAG, "Key %s not found, setting to default %u", key, *value);
    err = nvs_set_u16(my_handle, key, *value);
  }
  return err;
}

esp_err_t nvram_store_set_u16(nvs_handle my_handle, const char *key, uint16_t *value) {
  esp_err_t err;
  err = nvs_set_u16(my_handle, key, *value);
  nvs_commit(my_handle);
  return err;
}

esp_err_t nvram_store_get_u8(nvs_handle my_handle, const char *key, uint8_t *value, void *default_value) {
  esp_err_t err = nvs_get_u8(my_handle, key, value);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Then set it
    *value = *(uint8_t *) default_value;
    ESP_LOGI(TAG, "Key %s not found, setting to default %u", key, *value);
    err = nvs_set_u8(my_handle, key, *value);
  }
  return err;
}

esp_err_t nvram_store_set_u8(nvs_handle my_handle, const char *key, uint8_t *value) {
  esp_err_t err;
  err = nvs_set_u8(my_handle, key, *value);
  nvs_commit(my_handle);
  return err;
}

