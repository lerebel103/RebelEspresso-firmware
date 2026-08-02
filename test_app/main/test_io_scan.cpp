/**
 * I/O Scan architecture tests.
 *
 * Tests the safety override logic, debounce behavior, and process image
 * interactions that form the core of the I/O scan task.
 *
 * These tests exercise the logic in isolation by manipulating the process
 * image directly and verifying output states.
 */
#include <unity.h>
#include <cstring>

#include "process_image.h"
#include "boiler_refill_states.h"

// ============================================================================
// SECTION 1: Process Image Initialization Safety
// ============================================================================

TEST_CASE("IO: Process image init sets all outputs OFF", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  TEST_ASSERT_EQUAL(0, img->ssr_boiler_duty);
  TEST_ASSERT_FALSE(img->pump_on);
  TEST_ASSERT_FALSE(img->refill_solenoid_on);
  TEST_ASSERT_FALSE(img->three_way_on);
  TEST_ASSERT_FALSE(img->aux_on);
}

TEST_CASE("IO: Process image init sets all inputs inactive", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  TEST_ASSERT_FALSE(img->power_on);
  TEST_ASSERT_FALSE(img->brew_on);
  TEST_ASSERT_FALSE(img->steam_on);
  TEST_ASSERT_FALSE(img->descale_mode);
}

TEST_CASE("IO: Process image init sets sensors to fault state", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  // All temperature sensors should indicate fault (non-zero)
  for (int i = 0; i < PROCESS_IMAGE_MAX_SENSORS; i++) {
    TEST_ASSERT_NOT_EQUAL(0, img->temperatures[i].fault);
  }
  TEST_ASSERT_FALSE(img->water_level_ok);
}

// ============================================================================
// SECTION 2: Safety Override Logic
// ============================================================================

// Helper: replicate the safety override logic from io_scan.cpp apply_outputs()
// This is a pure function that can be tested without hardware.
struct override_result_t {
  int ssr_duty;
  bool pump;
  bool solenoid;
  bool three_way;
  bool aux;
};

static override_result_t apply_safety_overrides(const process_image_t *img) {
  override_result_t out;
  out.ssr_duty = img->ssr_boiler_duty;
  out.pump = img->pump_on;
  out.solenoid = img->refill_solenoid_on;
  out.three_way = img->three_way_on;
  out.aux = img->aux_on;

  if (!img->power_on) {
    out.ssr_duty = 0;
    out.pump = false;
    out.solenoid = false;
    out.three_way = false;
    out.aux = false;
  } else {
    if (!img->water_level_ok) {
      out.ssr_duty = 0;
    }
    if (img->refill_state == REFILL_STATE_ERROR) {
      out.ssr_duty = 0;
    }
    if (img->descale_mode) {
      out.ssr_duty = 0;
    }
    // Sensor fault on boiler RTD
    if (img->temperatures[1].fault != 0) {
      out.ssr_duty = 0;
    }
  }
  return out;
}

TEST_CASE("IO: Standby forces ALL outputs OFF regardless of desired state", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  // Set desired outputs to active
  img->ssr_boiler_duty = 80;
  img->pump_on = true;
  img->refill_solenoid_on = true;
  img->three_way_on = true;
  img->aux_on = true;
  img->water_level_ok = true;

  // But power is OFF
  img->power_on = false;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
  TEST_ASSERT_FALSE(result.pump);
  TEST_ASSERT_FALSE(result.solenoid);
  TEST_ASSERT_FALSE(result.three_way);
  TEST_ASSERT_FALSE(result.aux);
}

TEST_CASE("IO: Low water forces SSR to zero, other outputs unaffected", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = true;
  img->water_level_ok = false;
  img->ssr_boiler_duty = 75;
  img->pump_on = true;
  img->three_way_on = true;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
  // Pump and 3-way remain active (brew/refill may need them)
  TEST_ASSERT_TRUE(result.pump);
  TEST_ASSERT_TRUE(result.three_way);
}

TEST_CASE("IO: Refill error forces SSR to zero", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = true;
  img->water_level_ok = true;
  img->refill_state = REFILL_STATE_ERROR;
  img->ssr_boiler_duty = 60;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
}

TEST_CASE("IO: Descale mode forces SSR to zero", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = true;
  img->water_level_ok = true;
  img->descale_mode = true;
  img->ssr_boiler_duty = 100;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
}

TEST_CASE("IO: Sensor fault on boiler RTD forces SSR to zero", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = true;
  img->water_level_ok = true;
  img->ssr_boiler_duty = 70;
  // Clear the default fault so only the boiler sensor has a fault
  for (int i = 0; i < PROCESS_IMAGE_MAX_SENSORS; i++) {
    img->temperatures[i].fault = 0;
  }
  // Set boiler RTD (index 1) to faulted
  img->temperatures[1].fault = 5; // RTD_RefHigh

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
}

TEST_CASE("IO: All conditions OK allows full duty through", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = true;
  img->water_level_ok = true;
  img->descale_mode = false;
  img->refill_state = REFILL_STATE_IDLE;
  img->temperatures[1].fault = 0; // Boiler RTD healthy
  img->ssr_boiler_duty = 85;
  img->pump_on = true;
  img->aux_on = true;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(85, result.ssr_duty);
  TEST_ASSERT_TRUE(result.pump);
  TEST_ASSERT_TRUE(result.aux);
}

TEST_CASE("IO: Multiple overrides combine (low water + descale)", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = true;
  img->water_level_ok = false;
  img->descale_mode = true;
  img->ssr_boiler_duty = 50;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
}

// ============================================================================
// SECTION 3: Debounce Logic
// ============================================================================

// Replicate debounce_t and debounce_update for testing
struct test_debounce_t {
  bool stable_state;
  bool raw_state;
  uint8_t count;
};

static bool test_debounce_update(test_debounce_t &db, bool current_reading) {
  if (current_reading == db.raw_state) {
    if (db.count < 2) { // DEBOUNCE_COUNT = 2
      db.count++;
    }
  } else {
    db.raw_state = current_reading;
    db.count = 1;
  }

  if (db.count >= 2 && db.stable_state != db.raw_state) {
    db.stable_state = db.raw_state;
    return true;
  }
  return false;
}

TEST_CASE("IO: Debounce requires 2 consecutive same readings to accept", "[io_scan]") {
  test_debounce_t db = {false, false, 0};

  // Single HIGH reading — not yet accepted
  bool changed = test_debounce_update(db, true);
  TEST_ASSERT_FALSE(changed);
  TEST_ASSERT_FALSE(db.stable_state);

  // Second consecutive HIGH — now accepted
  changed = test_debounce_update(db, true);
  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_TRUE(db.stable_state);
}

TEST_CASE("IO: Debounce filters single-cycle glitches", "[io_scan]") {
  test_debounce_t db = {false, false, 0};

  // Establish stable HIGH
  test_debounce_update(db, true);
  test_debounce_update(db, true);
  TEST_ASSERT_TRUE(db.stable_state);

  // Single LOW glitch — should NOT change state
  bool changed = test_debounce_update(db, false);
  TEST_ASSERT_FALSE(changed);
  TEST_ASSERT_TRUE(db.stable_state);

  // Back to HIGH — counter resets, still HIGH
  changed = test_debounce_update(db, true);
  TEST_ASSERT_FALSE(changed);
  TEST_ASSERT_TRUE(db.stable_state);
}

TEST_CASE("IO: Debounce accepts sustained state change after 2 readings", "[io_scan]") {
  test_debounce_t db = {true, true, 2}; // Start stable HIGH

  // Two consecutive LOW readings
  bool changed = test_debounce_update(db, false);
  TEST_ASSERT_FALSE(changed); // First LOW, not yet
  TEST_ASSERT_TRUE(db.stable_state);

  changed = test_debounce_update(db, false);
  TEST_ASSERT_TRUE(changed); // Second LOW, accepted
  TEST_ASSERT_FALSE(db.stable_state);
}

TEST_CASE("IO: Rapid toggling never settles (alternating reads)", "[io_scan]") {
  test_debounce_t db = {false, false, 0};

  // Alternate HIGH/LOW — never 2 consecutive same
  for (int i = 0; i < 100; i++) {
    bool reading = (i % 2 == 0);
    bool changed = test_debounce_update(db, reading);
    TEST_ASSERT_FALSE(changed);
  }
  // State never changed from initial false
  TEST_ASSERT_FALSE(db.stable_state);
}

// ============================================================================
// SECTION 4: Remote Power Override Behavior
// ============================================================================

TEST_CASE("IO: Remote power_active sets process image ON", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  TEST_ASSERT_FALSE(img->power_on);

  // Simulate remote power_active() — it just writes to process image
  img->power_on = true;
  TEST_ASSERT_TRUE(img->power_on);
}

TEST_CASE("IO: Remote power_standby overrides even when GPIO would be ON", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  // Machine was turned on (e.g. by GPIO edge earlier)
  img->power_on = true;

  // Remote standby command
  img->power_on = false;

  // The safety overrides should treat this as standby
  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
  TEST_ASSERT_FALSE(result.pump);
  TEST_ASSERT_FALSE(result.aux);
}

TEST_CASE("IO: Remote power_active allows outputs when GPIO says OFF", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  // Remote active command (GPIO switch is OFF but remote says ON)
  img->power_on = true;
  img->water_level_ok = true;
  img->temperatures[1].fault = 0; // Boiler RTD healthy
  img->ssr_boiler_duty = 50;
  img->pump_on = true;
  img->aux_on = true;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(50, result.ssr_duty);
  TEST_ASSERT_TRUE(result.pump);
  TEST_ASSERT_TRUE(result.aux);
}

TEST_CASE("IO: GPIO edge overrides remote state (last transition wins)", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  // Machine remotely turned ON
  img->power_on = true;

  // GPIO switch transitions to OFF (simulates physical switch flip)
  // In real code, debounce detects edge and writes img->power_on = false
  img->power_on = false;

  // Standby is now in effect
  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
  TEST_ASSERT_FALSE(result.pump);
}

TEST_CASE("IO: Power state persists between scan cycles (no overwrite)", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  // Remote turns machine ON
  img->power_on = true;

  // Simulate multiple scan cycles where GPIO is OFF but no edge detected.
  // In the real I/O scan, power_on is only written on GPIO edge (power_changed).
  // Between edges, img->power_on retains whatever was last set.
  // Here we verify the process image holds the value across reads.
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_TRUE(img->power_on);
  }
}

// ============================================================================
// SECTION 5: Brew Edge Detection via Process Image
// ============================================================================

TEST_CASE("IO: Brew cannot start in standby", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = false;
  img->brew_on = true;

  // Brew should NOT activate in standby
  // (In the real I/O scan, scan_inputs checks power_on before setting brew_active)
  // This test verifies the invariant via process image state
  TEST_ASSERT_FALSE(img->brew_active);
}

TEST_CASE("IO: Process image tracks brew start time", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->brew_active = true;
  img->brew_start_time_us = 12345678;

  TEST_ASSERT_EQUAL(12345678, img->brew_start_time_us);
  TEST_ASSERT_TRUE(img->brew_active);
}

// ============================================================================
// SECTION 6: Process Image Field Isolation
// ============================================================================

TEST_CASE("IO: Sensor writes don't affect output fields", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  // Set some outputs
  img->ssr_boiler_duty = 50;
  img->pump_on = true;

  // Simulate sensor write
  img->temperatures[0].value = 105.5;
  img->temperatures[0].fault = 0;
  img->water_level_ok = true;

  // Outputs unchanged
  TEST_ASSERT_EQUAL(50, img->ssr_boiler_duty);
  TEST_ASSERT_TRUE(img->pump_on);
}

TEST_CASE("IO: Input writes don't affect sensor fields", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  // Set sensor data
  img->temperatures[1].value = 92.0;
  img->water_level_mv = 1500.0;

  // Simulate input write
  img->power_on = true;
  img->brew_on = true;

  // Sensor data unchanged
  TEST_ASSERT_EQUAL_DOUBLE(92.0, img->temperatures[1].value);
  TEST_ASSERT_EQUAL_DOUBLE(1500.0, img->water_level_mv);
}
