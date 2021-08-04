#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint-gcc.h>
#include <esp_event_base.h>

struct cJSON;

struct controller_cfg_t {
    bool enabled = false;
};


void controller_init(esp_event_loop_handle_t event_loop);

void controller_enter_loop();

void controller_cfg_to_json(cJSON *root, const char* base_key);
void controller_status_to_json(cJSON *root, const char* base_key);

void controller_handle_new_cfg(const cJSON* cfg);

#ifdef __cplusplus
}
#endif
