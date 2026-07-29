#include "shadow_helper.h"
#include "shadow/shadow_handler.h"
#include <esp_log.h>
#include <cstdlib>
#include <cJSON.h>
#include <cstring>

#define TAG "shadow_helper"

 void shadow_helper_send_shadow(device_shadow_handle_t shadow_handle, char *buffer, size_t len, cJSON* reported) {
  // Update config
  ESP_LOGI(TAG, "Sending reported shadow");
  cJSON *root = cJSON_CreateObject();
  cJSON *state = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "state", state);
  cJSON_AddItemToObject(state, "reported", reported);

  // Wipe desired configuration now that we are about to apply it.
  cJSON_AddItemToObject(state, "desired", cJSON_CreateNull());

  cJSON_PrintPreallocated(root, buffer, len, 0);
  len = strlen(buffer);

  shadow_handler_update(shadow_handle, buffer, len);
  cJSON_Delete(root);
}

void shadow_helper_apply_desired(device_shadow_handle_t shadow_handle, MQTTPublishInfo_t *pxPublishInfo, shadow_config_update_t cb) {
  ESP_LOGD(TAG, ">> Got update response: %.*s", pxPublishInfo->payloadLength, (char *) pxPublishInfo->pPayload);
  cJSON *root = cJSON_Parse((char *) pxPublishInfo->pPayload);

  // find the desired state block. It can be located at the top or under previous/current when the whole document is
  // received. Only pick-up current on startup
  static bool first = true;
  cJSON *state = cJSON_GetObjectItem(root, "state");
  if (first && !state) {
    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (current) {
      state = cJSON_GetObjectItem(current, "state");
      first = false;
    }
  }

  if (state) {
    cJSON *desired = cJSON_GetObjectItem(state, "desired");
    if (desired) {
      cb(desired);
    }
  }

  cJSON_Delete(root);
}