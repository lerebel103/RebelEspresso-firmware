#pragma once

#include <esp_event_base.h>

struct cJSON;

struct controller_cfg_t {
    bool enabled = false;
};

void controller_init(esp_event_loop_handle_t event_loop);
void controller_enter_loop();

const bool& controller_is_enabled();
void controller_enable(bool enable);


void controller_cfg_to_json(cJSON *root);
void controller_cfg_from_json(const cJSON* src);
