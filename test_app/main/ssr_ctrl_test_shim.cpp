/**
 * Test shim for ssr_ctrl (SSR controller).
 *
 * Provides a fake SSR controller that records duty values and power state
 * without any hardware timer or GPIO access. Used by safety interlock tests
 * to verify that safety code correctly forces duty to zero.
 */
#include "ssr_ctrl.h"
#include <cstdlib>
#include <esp_log.h>

static const char *TAG = "ssr_shim";

struct ssr_ctrl_t {
  ssr_ctrl_config_t cfg;
  int duty;
  bool is_on;
};

esp_err_t ssr_ctrl_new(ssr_ctrl_config_t cfg, ssr_ctrl_handle_t *ret_handle) {
  if (!ret_handle) {
    return ESP_ERR_INVALID_ARG;
  }

  auto *handle = (ssr_ctrl_t *)calloc(1, sizeof(ssr_ctrl_t));
  if (!handle) {
    return ESP_ERR_NO_MEM;
  }

  handle->cfg = cfg;
  handle->duty = 0;
  handle->is_on = false;

  *ret_handle = handle;
  ESP_LOGI(TAG, "Created mock SSR for GPIO %d", cfg.gpio);
  return ESP_OK;
}

esp_err_t ssr_ctrl_del(ssr_ctrl_handle_t handle) {
  if (!handle) {
    return ESP_ERR_INVALID_ARG;
  }
  free(handle);
  return ESP_OK;
}

esp_err_t ssr_ctrl_set_duty(ssr_ctrl_handle_t handle, int duty) {
  if (!handle) {
    return ESP_ERR_INVALID_ARG;
  }

  // Clamp exactly as the real implementation does
  if (duty > 100) {
    duty = 100;
  } else if (duty < 0) {
    duty = 0;
  }

  handle->duty = duty;
  return ESP_OK;
}

esp_err_t ssr_ctrl_get_duty(ssr_ctrl_handle_t handle, int &duty) {
  if (!handle) {
    return ESP_ERR_INVALID_ARG;
  }
  duty = handle->duty;
  return ESP_OK;
}

esp_err_t ssr_ctrl_power_off(ssr_ctrl_handle_t handle) {
  if (!handle) {
    return ESP_ERR_INVALID_ARG;
  }

  ssr_ctrl_set_duty(handle, 0);
  handle->is_on = false;
  ESP_LOGI(TAG, "Mock SSR power OFF");
  return ESP_OK;
}

esp_err_t ssr_ctrl_power_on(ssr_ctrl_handle_t handle) {
  if (!handle) {
    return ESP_ERR_INVALID_ARG;
  }

  handle->is_on = true;
  ESP_LOGI(TAG, "Mock SSR power ON");
  return ESP_OK;
}
