#include "connectivity.h"

#include "common/identity.h"
#include "common/nvs.h"
#include "wifi/wifi_manager.h"

#include <esp_app_desc.h>
#include <esp_log.h>

#define TAG "connectivity"

static EventGroupHandle_t xNetworkEventGroup;

void connectivity_init(EventGroupHandle_t net_group) {
  xNetworkEventGroup = net_group;

  ESP_ERROR_CHECK(nvs_init());

  auto identity = identity_get();
  ESP_LOGI(TAG, "--------------------------------------------------------------------------------");
  ESP_LOGI(TAG, "    Thing ID:      %s", identity_thing_id());
  ESP_LOGI(TAG, "    Thing Type:    %s", identity->thing_type);
  ESP_LOGI(TAG, "    Hardware rev:  %d.%d", identity->hardware_major, identity->hardware_minor);
  ESP_LOGI(TAG, "    Firmware ver:  %s", esp_app_get_description()->version);
  ESP_LOGI(TAG, "--------------------------------------------------------------------------------");

  wifi_manager_init(xNetworkEventGroup);
}
