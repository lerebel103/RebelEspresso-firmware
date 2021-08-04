#pragma once

#include <esp_event_base.h>

void pump_init(esp_event_loop_handle_t event_loop);

void pump_update(uint64_t  time_us);
