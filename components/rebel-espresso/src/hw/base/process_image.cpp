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
  s_image.water_level_ok = false;

  // State
  s_image.refill_state = REFILL_STATE_UNKNOWN;
  s_image.brew_active = false;
  s_image.brew_start_time_us = 0;
  s_image.last_scan_time_us = 0;
}
