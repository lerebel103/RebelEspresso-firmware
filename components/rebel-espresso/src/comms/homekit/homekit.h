#pragma once

#include <freertos/FreeRTOS.h>
#include <esp_event_base.h>

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

void homekit_init();

void homekit_terminate();

/**
 * Reconcile the running HomeKit subsystem with the current config after a
 * runtime config change: start it when newly enabled, stop it when disabled.
 */
void homekit_apply_config();

/**
 * Erase all HomeKit pairings (and the accessory identity) so the machine can be
 * paired again from scratch. Asynchronous — the accessory reboots afterwards.
 * Returns true when the reset was dispatched (HomeKit running), false otherwise.
 */
bool homekit_reset_pairings();

/// True while the HomeKit accessory task is running (i.e. enabled in config).
bool homekit_is_running();

/// Number of paired HomeKit controllers (0 = advertising, not yet paired).
int homekit_paired_count();

/// True when at least one paired HomeKit controller currently has an active session.
bool homekit_has_active_connection();

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
