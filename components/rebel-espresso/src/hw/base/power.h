#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event_base.h>

void power_init();

/**
 * Forces standby, unless the switch is in the on position
 */
void power_standby();

/**
 * Activates power
 */
void power_active();

bool power_is_active();

#ifdef __cplusplus
}
#endif
