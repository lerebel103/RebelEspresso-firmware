#include "process_image.h"
#include <cstring>

static process_image_t s_image;

extern "C" process_image_t *process_image_get(void) {
  return &s_image;
}

extern "C" void process_image_init(void) {
  memset(&s_image, 0, sizeof(s_image));

  // Explicit safe defaults for outputs
  s_image.ssr_boiler_duty = 0;
  s_image.ssr_applied_duty = 0;
  s_image.pump_on = false;
  s_image.refill_solenoid_on = false;
  s_image.three_way_on = false;
  s_image.aux_on = false;

  // Inputs default to inactive (safe)
  s_image.power_on = false;
  s_image.brew_on = false;
  s_image.steam_on = false;
  s_image.descale_mode = false;

  // Sensors default to faulted/unknown
  for (int i = 0; i < PROCESS_IMAGE_MAX_SENSORS; i++) {
    s_image.temperatures[i].value = 0;
    s_image.temperatures[i].fault = 1; // Non-zero = fault, inhibits heater
  }
  s_image.water_level_mv = 0;
  s_image.water_level_median_mv = 0;
  s_image.water_level_ok = false;

  // State
  s_image.refill_state = REFILL_STATE_UNKNOWN;
  s_image.brew_active = false;
  s_image.brew_start_time_us = 0;
  s_image.last_scan_time_us = 0;
}

extern "C" void process_image_enter_standby(process_image_t *img) {
  // Latched actuation + machine-state fields owned by the I/O scan. Sensor and
  // control-loop fields (temperatures, water level, ssr_boiler_duty) are left to
  // their owners; the safety gate forces the physical SSR/relays OFF while
  // power_on is false regardless.
  img->descale_mode = false;
  img->brew_active = false;
  img->pump_on = false;
  img->three_way_on = false;
  img->refill_solenoid_on = false;
  img->aux_on = false;
  img->refill_state = REFILL_STATE_UNKNOWN;
}

extern "C" void process_image_write_temp(uint8_t idx, measure_t data) {
  if (idx >= PROCESS_IMAGE_MAX_SENSORS) {
    return;
  }
  // seqlock write: bump to odd, publish, bump to even.
  // Use plain assignment (not ++/+=) to avoid the C++ volatile-compound-op warning.
  uint32_t seq = s_image.temp_seq[idx];
  s_image.temp_seq[idx] = seq + 1U; // odd → write in progress
  __sync_synchronize();
  s_image.temperatures[idx] = data;
  __sync_synchronize();
  s_image.temp_seq[idx] = seq + 2U; // even → complete
}

extern "C" measure_t process_image_read_temp(uint8_t idx) {
  measure_t out = {};
  if (idx >= PROCESS_IMAGE_MAX_SENSORS) {
    return out;
  }
  // seqlock read: retry while a write is in progress or the counter changed.
  uint32_t s0;
  uint32_t s1;
  do {
    s0 = s_image.temp_seq[idx];
    __sync_synchronize();
    out = s_image.temperatures[idx];
    __sync_synchronize();
    s1 = s_image.temp_seq[idx];
  } while ((s0 & 1U) || (s0 != s1));
  return out;
}
