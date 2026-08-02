#pragma once

/**
 * Process Image — shared data structure bridging all architectural layers.
 *
 * This is the single source of truth for the state of all physical I/O and
 * derived control values. Each field has exactly one owning layer that writes
 * it; other layers read only.
 *
 * Layers (highest to lowest priority):
 *   Layer 1 — I/O Scan (20ms, priority 8): reads switches, writes outputs
 *   Layer 2 — ADC/Sensor (5-10Hz, priority 6): reads temperatures, water level
 *   Layer 3 — Control Loop (1Hz, priority 7): runs PID, writes desired duties
 *   Layer 4 — Communication (1Hz, priority 3-5): web, display, HomeKit
 *
 * Thread safety:
 *   The I/O scan task runs at the highest priority and cannot be preempted by
 *   lower layers. All fields are naturally aligned (word-size on ESP32) making
 *   individual field reads/writes atomic. For multi-field consistency (e.g.
 *   temperature + fault), consumers should tolerate a one-cycle-old pairing.
 */

#include <cstdint>
#include <cstdbool>
#include "measure.h"
#include "boiler_refill_states.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum number of RTD sensor channels.
 * Matches RTD_MAX_COUNT from hw_config.h.
 */
#define PROCESS_IMAGE_MAX_SENSORS 4

struct process_image_t {
  // ─── INPUTS (written by I/O Scan task) ───────────────────────────────
  bool power_on;     ///< Debounced power switch state
  bool brew_on;      ///< Debounced brew switch state
  bool steam_on;     ///< Debounced steam switch state
  bool descale_mode; ///< Set on power-on if brew switch is held

  // ─── SENSOR DATA (written by ADC/Sensor task) ────────────────────────
  measure_t temperatures[PROCESS_IMAGE_MAX_SENSORS]; ///< Per-channel temp + fault
  double water_level_mv;                             ///< Raw water level ADC voltage
  bool water_level_ok;                               ///< Derived: mv <= threshold

  // ─── DESIRED OUTPUTS (written by Control Loop) ───────────────────────
  int ssr_boiler_duty;     ///< 0-100, computed by PID
  bool pump_on;            ///< Desired pump relay state
  bool refill_solenoid_on; ///< Desired refill valve state
  bool three_way_on;       ///< Desired 3-way valve state
  bool aux_on;             ///< Auxiliary output relay

  // ─── STATE (written by I/O Scan task) ────────────────────────────────
  RefillState_t refill_state;  ///< Current refill state machine state
  bool brew_active;            ///< Brew shot in progress
  uint64_t brew_start_time_us; ///< Timestamp when current brew started
  uint64_t last_scan_time_us;  ///< Timestamp of last I/O scan completion
};

/**
 * Get the singleton process image instance.
 * All layers access the same instance.
 */
process_image_t *process_image_get(void);

/**
 * Reset the process image to safe defaults.
 * Called once at boot before any task starts.
 * All outputs OFF, all inputs false, all sensors zeroed.
 */
void process_image_init(void);

#ifdef __cplusplus
}
#endif
