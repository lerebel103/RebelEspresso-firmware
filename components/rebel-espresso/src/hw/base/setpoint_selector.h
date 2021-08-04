#pragma once

#include <esp_event_base.h>

void setpoint_selector_init(esp_event_loop_handle_t event_loop);

void setpoint_selector_update(uint64_t  time_us);
