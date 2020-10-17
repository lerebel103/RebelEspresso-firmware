#pragma once

#include <cJSON.h>

#define OTA_CFG_JSON_KEY "ota."

/**
 * This is an async call, we compare our current firmware version
 * to what is available from the cloud side. If the cloud side is not
 * the same, we go ahead and pull that down, activate the new partition
 * and then reboot the device.
 *
 * mbed tls doesn't play well when placed on the stack as it competes for
 * resources on the stack, putting in its own task works way better.
 */
void ota_init(
        const char* thing_id,
        const char* thing_type,
        const char* firmware_version,
        const char* hardware_revision);

/**
 * Configure OTA
 */
void ota_update_cfg(const cJSON *config);

void ota_cfg_to_json(cJSON* config, const char* base_key);

bool ota_is_enabled();

/**
 * Run OTA in a separate task
 */
void ota_run();


int ota_get_duration();

uint32_t ota_get_error_count();
