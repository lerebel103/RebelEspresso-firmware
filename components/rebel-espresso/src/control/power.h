#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event_base.h>

void power_init(esp_event_loop_handle_t event_loop);

/**
 * Forces standby, unless the switch is in the on position
 */
void power_standby();

/**
 * Activates power
 */
void power_active();

bool power_is_active();