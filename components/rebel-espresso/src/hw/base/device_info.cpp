#include <nvs.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_app_desc.h>
#include "device_info.h"
#include "shadow_helper.h"
#include "common/identity.h"
#include "app_metrics.h"

#define TAG "device_info"

void device_info_handle_cfg(char *buffer, size_t max_len) {
  // Shadow publishing removed — device info is logged at boot only.
}

void device_info_init() {
  ESP_LOGI(TAG, "Device info initialised (shadow reporting disabled)");
}
