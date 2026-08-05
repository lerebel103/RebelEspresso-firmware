#pragma once

#include <freertos/FreeRTOS.h>
#include <esp_event_base.h>

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

void homekit_init();

void homekit_terminate();

/// True while the HomeKit accessory task is running (i.e. enabled in config).
bool homekit_is_running();

/// Number of paired HomeKit controllers (0 = advertising, not yet paired).
int homekit_paired_count();

/**
 * Notify HomeKit that the brew temperature setpoint has changed
 * (e.g., from the web interface). Pushes the update to subscribed clients.
 */
void homekit_notify_setpoint_changed(float setpoint);

/**
 * Notify HomeKit that the power state has changed
 * (e.g., from the web interface). Pushes the update to subscribed clients.
 */
void homekit_notify_power_changed(bool active);
