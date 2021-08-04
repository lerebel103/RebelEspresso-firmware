#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint-gcc.h>
#include <esp_event_base.h>
#include <cJSON.h>

void hw_specs_init(esp_event_loop_handle_t event_loop);

void hw_specs_cfg_to_json(cJSON *root, const char* base_key);
void hw_specs_status_to_json(cJSON *root, const char* base_key);

void hw_specs_handle_new_cfg(const cJSON* cfg);

#ifdef __cplusplus
}
#endif
