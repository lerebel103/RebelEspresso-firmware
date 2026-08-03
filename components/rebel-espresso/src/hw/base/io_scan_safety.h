#pragma once

/**
 * I/O Scan safety gate — pure, side-effect-free.
 *
 * This is the single authoritative safety gate that turns the desired output
 * state held in the process image into the outputs that may actually be driven
 * to hardware. It applies every heater-inhibit condition and the standby cutoff.
 *
 * It is deliberately free of hardware side effects so it can be unit-tested
 * directly (see test_app/main/test_io_scan.cpp). The I/O scan task calls this
 * function every cycle and then writes the returned values to hardware — no
 * other layer may drive the SSR or relays.
 */

#include "process_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Hard over-temperature limit (°C) enforced at the final output gate as
 * defense-in-depth. Matches the out-of-range ceiling in boiler_temp.cpp.
 * A valid boiler RTD reading above this cuts the SSR regardless of the duty
 * the control loop requested. A torn read of the temperature can only bias
 * toward cutting the heater, never toward energising it.
 */
#define IO_SCAN_OVERTEMP_LIMIT_C 140.0

/**
 * Safety-gated outputs computed from the process image.
 */
typedef struct {
  int ssr_duty;   ///< 0-100, after all heater-inhibit conditions
  bool pump;      ///< Pump relay
  bool solenoid;  ///< Refill solenoid relay
  bool three_way; ///< 3-way valve relay
  bool aux;       ///< Auxiliary relay
} io_scan_outputs_t;

/**
 * Compute the safety-gated outputs from the current process image.
 *
 * Rules (non-negotiable):
 *   - !power_on               → ALL outputs OFF
 *   - !water_level_ok         → SSR 0
 *   - refill_state == ERROR   → SSR 0
 *   - descale_mode            → SSR 0
 *   - boiler RTD fault        → SSR 0
 *   - boiler temp > limit     → SSR 0 (only when the reading is valid)
 *
 * No hardware is touched. Reads the boiler temperature through the seqlock
 * accessor so the fault/value pair is always a consistent snapshot.
 */
io_scan_outputs_t io_scan_apply_safety(const process_image_t *img);

#ifdef __cplusplus
}
#endif
