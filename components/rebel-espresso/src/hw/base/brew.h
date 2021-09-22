#pragma once

#include <esp_event_base.h>

void brew_init(esp_event_loop_handle_t event_loop);

void brew_update(uint64_t  time_us);
