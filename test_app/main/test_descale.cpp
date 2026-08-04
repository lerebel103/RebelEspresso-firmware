/**
 * Descale-mode and steam-setpoint behaviour tests.
 *
 * Descale entry (brew switch held at power-on) and the brew-path actuation
 * decisions are extracted into pure functions (io_scan_modes) so they can be
 * exercised without GPIO/FreeRTOS. The final safety gate (io_scan_apply_safety)
 * is the real production code — no logic is duplicated here.
 */
#include <unity.h>

#include "process_image.h"
#include "io_scan_safety.h"
#include "io_scan_modes.h"
#include "setpoint_selector.h"
#include "hw_config.h"

// ============================================================================
// Descale entry
// ============================================================================

TEST_CASE("Descale: entered when brew switch held at power-on", "[descale]") {
  TEST_ASSERT_TRUE(io_scan_descale_on_power_up(true));
}

TEST_CASE("Descale: not entered when brew switch off at power-on", "[descale]") {
  TEST_ASSERT_FALSE(io_scan_descale_on_power_up(false));
}

// ============================================================================
// Brew-path actuation in descale vs normal brew
// ============================================================================

TEST_CASE("Descale: brew start opens pump, 3-way AND refill solenoid", "[descale]") {
  auto o = io_scan_brew_started(/*descale=*/true, /*solenoid_now=*/false);
  TEST_ASSERT_TRUE(o.brew_active);
  TEST_ASSERT_TRUE(o.pump);
  TEST_ASSERT_TRUE(o.three_way);
  TEST_ASSERT_TRUE(o.solenoid); // descale routes flow through the fill solenoid
}

TEST_CASE("Normal brew start does NOT touch the refill solenoid", "[descale]") {
  // Solenoid stays under refill ownership (whatever it was).
  auto off = io_scan_brew_started(/*descale=*/false, /*solenoid_now=*/false);
  TEST_ASSERT_TRUE(off.pump);
  TEST_ASSERT_TRUE(off.three_way);
  TEST_ASSERT_FALSE(off.solenoid);

  auto on = io_scan_brew_started(/*descale=*/false, /*solenoid_now=*/true);
  TEST_ASSERT_TRUE(on.solenoid); // pass-through, not forced
}

TEST_CASE("Descale: brew stop closes solenoid and pump together", "[descale]") {
  auto o = io_scan_brew_stopped(/*descale=*/true, /*solenoid_now=*/true);
  TEST_ASSERT_FALSE(o.brew_active);
  TEST_ASSERT_FALSE(o.pump);
  TEST_ASSERT_FALSE(o.three_way);
  TEST_ASSERT_FALSE(o.solenoid);
}

TEST_CASE("Normal brew stop keeps pump while auto-refill holds the solenoid", "[descale]") {
  auto refilling = io_scan_brew_stopped(/*descale=*/false, /*solenoid_now=*/true);
  TEST_ASSERT_TRUE(refilling.pump);     // refill still needs the pump
  TEST_ASSERT_TRUE(refilling.solenoid); // left untouched

  auto idle = io_scan_brew_stopped(/*descale=*/false, /*solenoid_now=*/false);
  TEST_ASSERT_FALSE(idle.pump);
  TEST_ASSERT_FALSE(idle.solenoid);
}

// ============================================================================
// Descale + final safety gate (real production gate)
// ============================================================================

TEST_CASE("Descale: gate forces SSR off but lets the solenoid actuate", "[descale]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = true;
  img->water_level_ok = true;
  img->descale_mode = true;
  img->temperatures[RTD_BREW_BOILER_IDX].fault = 0;

  // Brew activated during descale drives pump + solenoid.
  auto o = io_scan_brew_started(img->descale_mode, img->refill_solenoid_on);
  img->pump_on = o.pump;
  img->refill_solenoid_on = o.solenoid;
  img->three_way_on = o.three_way;
  img->ssr_boiler_duty = 100; // heater "wants" to run

  auto out = io_scan_apply_safety(img);
  TEST_ASSERT_EQUAL(0, out.ssr_duty); // heater inhibited in descale
  TEST_ASSERT_TRUE(out.solenoid);     // solenoid still actuates for descaling
  TEST_ASSERT_TRUE(out.pump);
}

// ============================================================================
// Steam switch -> secondary boiler PID setpoint
// ============================================================================

TEST_CASE("Steam switch selects the secondary boiler setpoint", "[steam]") {
  TEST_ASSERT_EQUAL(1, setpoint_selector_index(true));
  TEST_ASSERT_EQUAL(0, setpoint_selector_index(false));
}
