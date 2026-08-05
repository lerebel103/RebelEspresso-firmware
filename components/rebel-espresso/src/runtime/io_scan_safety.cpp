#include "io_scan_safety.h"
#include "boiler_refill_states.h"
#include "water_probe.h"
#include <hw_config.h>

io_scan_outputs_t io_scan_apply_safety(const process_image_t *img) {
  io_scan_outputs_t out;
  out.ssr_duty = img->ssr_boiler_duty;
  out.pump = img->pump_on;
  out.solenoid = img->refill_solenoid_on;
  out.three_way = img->three_way_on;
  out.aux = img->aux_on;

  // ─── SAFETY OVERRIDES (non-negotiable) ───────────────────────────────
  if (!img->power_on) {
    // Standby: ALL outputs OFF
    out.ssr_duty = 0;
    out.pump = false;
    out.solenoid = false;
    out.three_way = false;
    out.aux = false;
    return out;
  }

  // Power is on — apply conditional heater-inhibit overrides.
  if (!img->water_level_ok) {
    out.ssr_duty = 0;
  }
  if (img->refill_state == REFILL_STATE_ERROR) {
    out.ssr_duty = 0;
  }
  if (img->descale_mode) {
    out.ssr_duty = 0;
  }
  if (img->level_status == LEVEL_UNKNOWN) {
    // Untrusted level (ADC fault / out-of-range / corroded probe): heater safe.
    out.ssr_duty = 0;
  }

  // Read the boiler RTD as a consistent (fault, value) snapshot.
  measure_t boiler = process_image_read_temp(RTD_BREW_BOILER_IDX);
  if (boiler.fault != 0) {
    // Sensor fault — cannot safely control the heater.
    out.ssr_duty = 0;
  } else if (boiler.value > IO_SCAN_OVERTEMP_LIMIT_C) {
    // Over-temperature hard cutoff (defense-in-depth).
    out.ssr_duty = 0;
  }

  return out;
}
