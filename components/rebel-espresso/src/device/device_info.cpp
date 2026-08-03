#include <nvs.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_app_desc.h>
#include "device_info.h"
#include "common/identity.h"
#include "app_metrics.h"

#define TAG "device_info"

void device_info_init() {
  ESP_LOGI(TAG, "Device info initialised");
}
