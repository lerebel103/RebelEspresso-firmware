#pragma once

#include <esp_event_base.h>

void setpoint_selector_init();

void setpoint_selector_update(uint64_t  time_us);
