#pragma once

#include <cstdlib>
#include <cJSON.h>
#include <cstring>
#include <esp_err.h>

// Stub types — shadow/MQTT functionality has been removed.
// These stubs allow existing code to compile without changes.
typedef void *device_shadow_handle_t;
typedef void (*shadow_callback_t)(void *, void *);
typedef void (*shadow_config_update_t)(const cJSON *desired);

struct device_shadow_cfg_t {
  char name[64];
  shadow_callback_t get;
  shadow_callback_t updated;
  shadow_callback_t deleted;
};

// No-op stubs
static inline void null_shadow_handler(void *, void *) {}

static inline void shadow_helper_send_shadow(device_shadow_handle_t, char *, size_t, cJSON *reported) {
  if (reported) {
    cJSON_Delete(reported);
  }
}

static inline void shadow_helper_apply_desired(device_shadow_handle_t, void *, shadow_config_update_t) {}

static inline esp_err_t shadow_handler_init(device_shadow_cfg_t, device_shadow_handle_t *) {
  return ESP_OK;
}

static inline esp_err_t shadow_handler_update(device_shadow_handle_t, char *, size_t) {
  return ESP_OK;
}
