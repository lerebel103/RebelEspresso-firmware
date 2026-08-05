#pragma once

#include <esp_event_base.h>

void setpoint_selector_init();

void setpoint_selector_update(uint64_t time_us);

/// Boiler PID setpoint index selected by the steam switch (0 = brew, 1 = steam).
int setpoint_selector_index(bool steam_on);
