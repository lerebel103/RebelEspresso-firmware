/**
 * Shims for safety tests.
 *
 * Provides minimal stubs for functions that boiler_temp.cpp, brew_temp.cpp,
 * and boiler_refill_states.cpp depend on but that aren't available in the
 * test_app build (no real hardware, no I2C, no GPIO toggling, etc.)
 */
#include <esp_err.h>
#include <cstdint>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_event.h>

#include "events.h"
#include "out_signals.h"
#include "app_metrics.h"
#include "brew_temp.h"

// Define the event bases used by production code
ESP_EVENT_DEFINE_BASE(MACHINE_EVENTS);
ESP_EVENT_DEFINE_BASE(BUTTONS_EVENTS);

// ---- app_metrics stub ----
void app_metrics_init() {}
void app_metrics_send(time_t, char *, size_t) {}
bool app_metrics_update_required(int) { return false; }
void app_metrics_reset_update() {}
device_metrics_t app_metrics_get() { return {}; }

// ---- brew_temp stub ----
// boiler_temp.cpp calls brew_temp_get_trim() during normal PID processing
static brew_temp_trim_t s_fake_trim = {.active = false, .value = 0};

brew_temp_trim_t brew_temp_get_trim() {
  return s_fake_trim;
}

// ---- out_signals stub ----
// Tracks relay state in memory for test assertions
static uint8_t s_relay_state[4] = {0, 0, 0, 0};

void out_signals_set_level(enum out_signals_t slot, uint8_t level) {
  if (slot <= OUT_SIGNALS_AUX) {
    s_relay_state[slot] = level;
  }
}

uint8_t out_signals_get_level(enum out_signals_t slot) {
  if (slot <= OUT_SIGNALS_AUX) {
    return s_relay_state[slot];
  }
  return 0;
}

void out_signals_init() {
  for (int i = 0; i < 4; i++) {
    s_relay_state[i] = 0;
  }
}

extern "C" {

// Exposed for test assertions
uint8_t test_shim_get_relay(int slot) {
  return s_relay_state[slot];
}

void test_shim_reset_relays() {
  for (int i = 0; i < 4; i++) {
    s_relay_state[i] = 0;
  }
}

} // extern "C"

// ---- esp_restart override ----
// We use the --wrap linker flag to intercept esp_restart() calls.
// The wrapper only intercepts when s_intercept_restart is true (set by test).
// Otherwise, it calls the real esp_restart via __real_esp_restart.
static bool s_restart_called = false;
static bool s_intercept_restart = false;

extern "C" void __real_esp_restart(void);

extern "C" {

void __wrap_esp_restart(void) {
  if (s_intercept_restart) {
    s_restart_called = true;
    // Don't actually restart — just record it
    return;
  }
  // Not intercepting — call the real restart (panic handler, etc.)
  __real_esp_restart();
}

bool test_shim_restart_called() {
  return s_restart_called;
}

void test_shim_reset_restart() {
  s_restart_called = false;
}

void test_shim_intercept_restart(bool intercept) {
  s_intercept_restart = intercept;
  s_restart_called = false;
}

} // extern "C"
