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
 *   lower layers. 32-bit aligned fields (bool, int, float, pointers) are
 *   naturally atomic on ESP32. 64-bit fields (double, uint64_t) may exhibit
 *   torn reads if accessed concurrently; consumers should tolerate a
 *   one-cycle-old value for those non-safety fields.
 *   The temperatures[] array (measure_t: value + fault) is a multi-word value
 *   that safety decisions depend on (fault gating AND the over-temp value
 *   cutoff), so it is NOT read directly. It is written by the Sensor task via
 *   process_image_write_temp() and read via process_image_read_temp(), a
 *   per-channel seqlock that always returns a consistent (value, fault)
 *   snapshot.
 *
 * Ownership model (see docs/../.kiro/specs/realtime-io-parity-audit.md §2):
 *   Every field has exactly ONE writer layer; all other layers read only.
 *   Each field below is tagged @owner. Do not write a field from a layer that
 *   does not own it. `power_on` is the single deliberate exception: it is
 *   written by BOTH the I/O scan (on a debounced GPIO edge) and the remote
 *   power_active()/power_standby() API. Both express the same semantic and the
 *   "last transition wins" rule keeps the outcome deterministic.
 */

#include <cstdint>
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
  bool power_on;     ///< @owner I/O Scan (edge) + remote power API. Debounced power switch state
  bool brew_on;      ///< @owner I/O Scan. Debounced brew switch state
  bool steam_on;     ///< @owner I/O Scan. Debounced steam switch state
  bool descale_mode; ///< @owner I/O Scan. Set on power-on if brew switch is held

  // ─── SENSOR DATA (written by ADC/Sensor task) ────────────────────────
  measure_t temperatures[PROCESS_IMAGE_MAX_SENSORS]; ///< @owner Sensor task. Per-channel temp + fault
  double water_level_mv;                             ///< @owner Sensor task. Raw water level ADC voltage
  bool water_level_ok;                               ///< @owner Sensor task. Derived: mv <= threshold

  // ─── DESIRED OUTPUTS ─────────────────────────────────────────────────
  //   ssr_boiler_duty is written by the Control Loop (PID).
  //   The relay states are written by the I/O Scan (brew/refill logic).
  int ssr_boiler_duty;     ///< @owner Control Loop. 0-100, computed by PID
  bool pump_on;            ///< @owner I/O Scan. Desired pump relay state
  bool refill_solenoid_on; ///< @owner I/O Scan. Desired refill valve state
  bool three_way_on;       ///< @owner I/O Scan. Desired 3-way valve state
  bool aux_on;             ///< @owner I/O Scan. Auxiliary output relay

  // ─── STATE (written by I/O Scan task) ────────────────────────────────
  RefillState_t refill_state;  ///< @owner I/O Scan. Current refill state machine state
  bool brew_active;            ///< @owner I/O Scan. Brew shot in progress
  uint64_t brew_start_time_us; ///< @owner I/O Scan. Timestamp when current brew started
  uint64_t last_scan_time_us;  ///< @owner I/O Scan. Timestamp of last I/O scan completion

  // ─── CONCURRENCY ─────────────────────────────────────────────────────
  ///< Per-channel seqlock counter guarding temperatures[] (measure_t is a
  ///< multi-word value that can tear). Even = stable, odd = write in progress.
  ///< Written by the Sensor task via process_image_write_temp(); readers use
  ///< process_image_read_temp() to obtain a consistent (value, fault) snapshot.
  volatile uint32_t temp_seq[PROCESS_IMAGE_MAX_SENSORS];
};

/**
 * Get the singleton process image instance.
 * All layers access the same instance.
 */
process_image_t *process_image_get(void);

/**
 * Reset the process image to safe defaults.
 * Called once at boot before any task starts.
 * All outputs OFF, all inputs false, and every temperature channel marked
 * faulted (fault != 0) with water_level_ok = false, so the heater stays
 * inhibited until the Sensor task publishes a real, valid reading.
 */
void process_image_init(void);

/**
 * Write a temperature channel using a seqlock so readers never observe a torn
 * (value, fault) pair. Only the Sensor task (the owner) should call this.
 */
void process_image_write_temp(uint8_t idx, measure_t data);

/**
 * Read a temperature channel as a consistent (value, fault) snapshot. Safe to
 * call from any layer; retries while a write is in progress. Returns a zeroed
 * measure_t for an out-of-range index.
 */
measure_t process_image_read_temp(uint8_t idx);

#ifdef __cplusplus
}
#endif
