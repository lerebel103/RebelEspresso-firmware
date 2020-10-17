//
// Created by dev on 5/28/19.
//
#include <string.h>
#include <esp_spi_flash.h>
#include <esp_system.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <malloc.h>

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>

#include "nvram_store.h"

const static char* TAG = "store";

uint32_t g_cycle_count = 0;


// Maximum we are going to wait for wifi connect etc... etc...



#define DEFAULT_OPERATION_TIMEOUT_SECONDS 30

#define NVRAM_STORAGE "storage"

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

}

void nvram_store_write_blob(const char *key, const char *data, size_t len, char **cache_ptr) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);

    if (err == ESP_OK) {
        ESP_ERROR_CHECK(nvs_set_blob(my_handle, key, data, len));
    }

    nvs_commit(my_handle);
    nvs_close(my_handle);

    // Clear data cache
    if (*cache_ptr != NULL) {
        free(*cache_ptr);
        *cache_ptr = NULL;
    }
}

const char *nvram_store_read_blob(const char *key, char **cache_ptr) {
    if (*cache_ptr == NULL) {
        nvs_handle my_handle;
        ESP_ERROR_CHECK(nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle));

        size_t len;
        esp_err_t err = nvs_get_blob(my_handle, key, NULL, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(my_handle);
            return "";
        }
        *cache_ptr = (char *) malloc(len * sizeof(char) + 1);
        ESP_ERROR_CHECK(nvs_get_blob(my_handle, key, *cache_ptr, &len));

        // Safety (but also a hack to treat as a string)
        if ((*cache_ptr)[len] != '\0') {
            (*cache_ptr)[len] = '\0';
        }

        nvs_close(my_handle);
    }
    return *cache_ptr;
}

esp_err_t nvram_store_read_u32(const char *key, uint32_t *value, uint32_t default_value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
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

esp_err_t nvram_store_get_u32(nvs_handle my_handle, const char *key, uint32_t *value, void* default_value) {
    esp_err_t err = nvs_get_u32(my_handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Then set it
        *value = *(uint32_t*)default_value;
        ESP_LOGI(TAG, "Key %s not found, setting to default %ul", key, *value);
        err = nvs_set_u32(my_handle, key, *value);
    }
    return err;
}

esp_err_t nvram_store_write_u32(const char *key, uint32_t value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Write
        err = nvram_store_set_u32(my_handle, key, &value);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvram_store_set_u32(nvs_handle my_handle, const char *key, uint32_t* value) {
    esp_err_t err;
    err = nvs_set_u32(my_handle, key, *value);
    nvs_commit(my_handle);
    return err;
}

esp_err_t nvram_store_get_u64(nvs_handle my_handle, const char *key, uint64_t *value, void* default_value) {
    esp_err_t err = nvs_get_u64(my_handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Then set it
        *value = *(uint64_t*)default_value;
        ESP_LOGI(TAG, "Key %s not found, setting to default %llu", key, *value);
        err = nvs_set_u64(my_handle, key, *value);
    }
    return err;
}

esp_err_t nvram_store_set_u64(nvs_handle my_handle, const char *key, uint64_t* value) {
    esp_err_t err;
    err = nvs_set_u64(my_handle, key, *value);
    nvs_commit(my_handle);
    return err;
}

esp_err_t nvram_store_get_u16(nvs_handle my_handle, const char *key, uint16_t *value, void* default_value) {
    esp_err_t err = nvs_get_u16(my_handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Then set it
        *value = *(uint16_t*)default_value;
        ESP_LOGI(TAG, "Key %s not found, setting to default %u", key, *value);
        err = nvs_set_u16(my_handle, key, *value);
    }
    return err;
}

esp_err_t nvram_store_set_u16(nvs_handle my_handle, const char *key, uint16_t* value) {
    esp_err_t err;
    err = nvs_set_u16(my_handle, key, *value);
    nvs_commit(my_handle);
    return err;
}

esp_err_t nvram_store_get_u8(nvs_handle my_handle, const char *key, uint8_t *value, void* default_value) {
    esp_err_t err = nvs_get_u8(my_handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Then set it
        *value = *(uint8_t*)default_value;
        ESP_LOGI(TAG, "Key %s not found, setting to default %u", key, *value);
        err = nvs_set_u8(my_handle, key, *value);
    }
    return err;
}

esp_err_t nvram_store_set_u8(nvs_handle my_handle, const char *key, uint8_t* value) {
    esp_err_t err;
    err = nvs_set_u8(my_handle, key, *value);
    nvs_commit(my_handle);
    return err;
}

esp_err_t nvram_store_read_i16(const char *key, int16_t *value, int16_t default_value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Read
        err = nvs_get_i16(my_handle, key, value);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            // Then set it
            *value = default_value;
            ESP_LOGI(TAG, "Key %s not found, setting to default %ul", key, *value);
            err = nvs_set_i16(my_handle, key, *value);
        }
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvram_store_write_i16(const char *key, int16_t value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Write
        err = nvs_set_i16(my_handle, key, value);
        nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvram_store_read_u16(const char *key, uint16_t *value, uint16_t default_value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Read
        err = nvs_get_u16(my_handle, key, value);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            // Then set it
            *value = default_value;
            ESP_LOGI(TAG, "Key %s not found, setting to default %ul", key, *value);
            err = nvs_set_u16(my_handle, key, *value);
        }
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvram_store_write_u16(const char *key, uint16_t value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Write
        err = nvs_set_u16(my_handle, key, value);
        nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvram_store_read_i32(const char *key, int32_t *value, int32_t default_value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Read
        err = nvram_store_get_i32(my_handle, key, value, &default_value);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t
nvram_store_get_i32(nvs_handle my_handle, const char *key, int32_t *value, void* default_value) {
    esp_err_t err = nvs_get_i32(my_handle, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Then set it
        *value = *(int32_t*)default_value;
        ESP_LOGI(TAG, "Key %s not found, setting to default %u", key, *value);
        err = nvs_set_i32(my_handle, key, *value);
    }
    return err;
}

esp_err_t nvram_store_write_i32(const char *key, int32_t value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Write
        err = nvram_store_set_i32(my_handle, key, &value);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvram_store_set_i32(nvs_handle my_handle, const char *key, int32_t* value) {
    esp_err_t err;
    err = nvs_set_i32(my_handle, key, *value);
    nvs_commit(my_handle);
    return err;
}


esp_err_t nvram_store_read_u8(const char *key, uint8_t *value, uint8_t default_value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Read
        err = nvs_get_u8(my_handle, key, value);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            // Then set it
            *value = default_value;
            ESP_LOGI(TAG, "Key %s not found, setting to default %ul", key, *value);
            err = nvs_set_u8(my_handle, key, *value);
        }
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvram_store_write_u8(const char *key, uint8_t value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Write
        err = nvs_set_u8(my_handle, key, value);
        nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}


esp_err_t nvram_store_read_bool(const char *key, bool *value, bool default_value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Read
        int8_t val;
        err = nvs_get_i8(my_handle, key, &val);
        *value = val;
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            // Then set it
            *value = default_value;
            ESP_LOGI(TAG, "Key %s not found, setting to default %d", key, (int8_t) (*value));
            err = nvs_set_i8(my_handle, key, (int8_t) (*value));
        }
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvram_store_write_bool(const char *key, bool value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        // Write
        err = nvs_set_i8(my_handle, key, (int8_t) (value));
        nvs_commit(my_handle);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvram_store_read_str(nvs_handle my_handle, const char *key, char *value, size_t max_len, const char *default_value) {
    size_t len = max_len;
    esp_err_t err = nvs_get_str(my_handle, key, value, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Then set it
        strcpy(value, default_value);
        ESP_LOGI(TAG, "Key %s not found, setting to default %s", key, value);
        err = nvs_set_str(my_handle, key, value);
    }
    return err;
}

esp_err_t nvram_store_read_str(const char *key, char *value, size_t max_len, const char *default_value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        err = nvram_store_read_str(my_handle, key, value, max_len, default_value);
    }
    nvs_close(my_handle);
    return err;
}

esp_err_t nvram_store_write_str(nvs_handle handle, const char *key, const char* value) {
    // Write
    esp_err_t err = nvs_set_str(handle, key, value);
    nvs_commit(handle);
    return err;
}

esp_err_t nvram_store_write_str(const char *key, const char *value) {
    nvs_handle my_handle;
    esp_err_t err = nvs_open(NVRAM_STORAGE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    } else {
        nvram_store_write_str(my_handle, key, value);
    }
    nvs_close(my_handle);
    return err;
}

void store_inc_cycle_count() {
    uint32_t count = store_get_cycle_count() + 1;
    esp_err_t err = nvram_store_write_u32("cycle_cnt", count);
    ESP_ERROR_CHECK(err);
    g_cycle_count = count;
}

uint32_t store_get_cycle_count() {
    if (g_cycle_count == 0) {
        // This is our default value
        uint32_t val;
        esp_err_t err = nvram_store_read_u32("cycle_cnt", &val, 1);
        ESP_ERROR_CHECK(err);
        g_cycle_count = val;
    }
    return g_cycle_count;
}


void store_set_operation_timeout_seconds(uint32_t timeout) {
    esp_err_t err = nvram_store_write_u32("op_timeout_s", timeout);
    ESP_ERROR_CHECK(err);
}

uint32_t store_get_operation_timeout_seconds() {
    // This is our default value
    uint32_t val;
    esp_err_t err = nvram_store_read_u32("op_timeout_s", &val, DEFAULT_OPERATION_TIMEOUT_SECONDS);
    ESP_ERROR_CHECK(err);
    return val;
}


