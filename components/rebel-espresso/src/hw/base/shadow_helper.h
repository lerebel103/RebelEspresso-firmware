#pragma once

#include <cstdlib>
#include <cJSON.h>
#include <cstring>
#include "shadow/shadow_handler.h"

typedef void (* shadow_config_update_t )( const cJSON* desired );

void shadow_helper_send_shadow(device_shadow_handle_t shadow_handle, char *buffer, size_t len, cJSON* reported);

void shadow_helper_apply_desired(device_shadow_handle_t shadow_handle, MQTTPublishInfo_t *pxPublishInfo, shadow_config_update_t cb);
