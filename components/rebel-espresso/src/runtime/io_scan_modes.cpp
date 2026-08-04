#include "io_scan_modes.h"

bool io_scan_descale_on_power_up(bool brew_switch_on) {
  return brew_switch_on;
}

brew_path_outputs_t io_scan_brew_started(bool descale_mode, bool solenoid_now) {
  brew_path_outputs_t o;
  o.brew_active = true;
  o.pump = true;
  o.three_way = true;
  o.solenoid = descale_mode ? true : solenoid_now;
  return o;
}

brew_path_outputs_t io_scan_brew_stopped(bool descale_mode, bool solenoid_now) {
  brew_path_outputs_t o;
  o.brew_active = false;
  o.three_way = false;
  if (descale_mode) {
    o.solenoid = false;
    o.pump = false;
  } else {
    // Leave a refill-owned solenoid alone; keep the pump only if it needs it.
    o.solenoid = solenoid_now;
    o.pump = solenoid_now;
  }
  return o;
}
