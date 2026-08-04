#pragma once

#include <stdbool.h>

/**
 * Pure mode/actuation decisions for the I/O scan, extracted so they can be
 * unit tested without GPIO or FreeRTOS. io_scan.cpp owns the timestamps, event
 * posting, and process-image writes; these functions only compute the desired
 * actuator states.
 */

/// Desired brew-path actuator states after a brew switch edge.
typedef struct {
  bool brew_active;
  bool pump;
  bool three_way;
  bool solenoid; ///< refill/fill solenoid (RELAY2)
} brew_path_outputs_t;

/// Action to take on a brew switch edge, honouring the descale start-up lockout.
typedef struct {
  bool start;   ///< begin brewing (drive outputs, post BREW_STARTED)
  bool stop;    ///< end brewing (post BREW_STOPPED)
  bool lockout; ///< updated descale brew-lockout latch
} brew_edge_action_t;

#ifdef __cplusplus
extern "C" {
#endif

/// On a power-on edge, descale is entered when the brew switch is (debounced) held on.
bool io_scan_descale_on_power_up(bool brew_switch_on);

/// Decide what a brew switch edge does, honouring the descale start-up lockout.
/// While `lockout` is set (descale was entered with the brew switch held), the
/// initial brew-on is ignored so the pump/peripherals stay off; the first
/// brew-off releases the lockout, after which a brew-on drives the descale pump.
brew_edge_action_t io_scan_brew_edge(bool brew_on, bool brew_active, bool power_on, bool lockout);

/// Actuator states when the brew switch turns ON. In descale the fill solenoid
/// also opens so descaler flows through the boiler fill path; otherwise the
/// solenoid is left under refill ownership (solenoid_now passed through).
brew_path_outputs_t io_scan_brew_started(bool descale_mode, bool solenoid_now);

/// Actuator states when the brew switch turns OFF. In descale the solenoid and
/// pump are closed together; otherwise the pump keeps running only while an
/// auto-refill still holds the solenoid open.
brew_path_outputs_t io_scan_brew_stopped(bool descale_mode, bool solenoid_now);

#ifdef __cplusplus
}
#endif
