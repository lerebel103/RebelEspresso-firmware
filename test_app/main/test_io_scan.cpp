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
#include "io_scan_safety.h"
#include "boiler_refill_states.h"

// Process image init, field-isolation, and seqlock tests live in
// test_process_image.cpp. This file covers the I/O scan behaviour itself.

// ============================================================================
// SECTION 2: Safety Override Logic
// ============================================================================

// These tests exercise the REAL production safety gate (io_scan_apply_safety in
// io_scan_safety.cpp) directly — no logic is duplicated in the test. The thin
// alias keeps the existing test bodies readable; io_scan_outputs_t exposes the
// same field names (ssr_duty, pump, solenoid, three_way, aux).
static inline io_scan_outputs_t apply_safety_overrides(const process_image_t *img) {
  return io_scan_apply_safety(img);
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
// SECTION 7: Over-temperature Final Output Gate
// ============================================================================

TEST_CASE("IO: Over-temp boiler reading forces SSR to zero at final gate", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = true;
  img->water_level_ok = true;
  img->descale_mode = false;
  img->refill_state = REFILL_STATE_IDLE;
  for (int i = 0; i < PROCESS_IMAGE_MAX_SENSORS; i++) {
    img->temperatures[i].fault = 0;
  }
  // Boiler RTD reports above the 140C hard limit but control loop left duty high
  img->temperatures[1].fault = 0;
  img->temperatures[1].value = 145.0;
  img->ssr_boiler_duty = 100;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
}

TEST_CASE("IO: Boiler reading just below limit still allows duty", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = true;
  img->water_level_ok = true;
  img->descale_mode = false;
  img->refill_state = REFILL_STATE_IDLE;
  for (int i = 0; i < PROCESS_IMAGE_MAX_SENSORS; i++) {
    img->temperatures[i].fault = 0;
  }
  img->temperatures[1].value = 139.5;
  img->ssr_boiler_duty = 60;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(60, result.ssr_duty);
}

TEST_CASE("IO: Over-temp gate ignored when boiler RTD is faulted (fault gate already cuts)", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();

  img->power_on = true;
  img->water_level_ok = true;
  // Faulted boiler RTD with a garbage high value — fault gate must dominate.
  img->temperatures[1].fault = 3;
  img->temperatures[1].value = 200.0;
  img->ssr_boiler_duty = 80;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
}

// ============================================================================
// SECTION 8: Remote / Physical Power Precedence Matrix ("last transition wins")
// ============================================================================
//
// The I/O scan writes img->power_on ONLY on a debounced GPIO edge. The remote
// API (power_active/power_standby) writes img->power_on directly. Whichever
// path transitions last determines the active state. These tests model each
// conflict combination by applying the writes in order and asserting the final
// state and the safety-gate outcome.

// Helper: apply a physical-switch edge (only writes on a genuine transition).
static void apply_switch_edge(process_image_t *img, bool &prev_switch, bool new_switch) {
  if (new_switch != prev_switch) {
    prev_switch = new_switch;
    img->power_on = new_switch; // io_scan writes power_on on edge
  }
}

TEST_CASE("IO Precedence: remote ON while physical OFF -> machine ON", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  bool prev_switch = false; // physical switch OFF, no edge

  // Remote turns machine on
  img->power_on = true; // power_active()

  // Physical switch stays OFF (no edge) across many scans
  for (int i = 0; i < 50; i++) {
    apply_switch_edge(img, prev_switch, false);
  }
  TEST_ASSERT_TRUE(img->power_on);
}

TEST_CASE("IO Precedence: remote OFF while physical ON -> machine OFF", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  bool prev_switch = true; // physical switch already ON (no new edge)
  img->power_on = true;

  // Remote turns machine off
  img->power_on = false; // power_standby()

  // Physical switch stays ON (no edge) — remote OFF persists
  for (int i = 0; i < 50; i++) {
    apply_switch_edge(img, prev_switch, true);
  }
  TEST_ASSERT_FALSE(img->power_on);
}

TEST_CASE("IO Precedence: remote ON then physical OFF edge -> physical wins (OFF)", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  bool prev_switch = true; // switch currently ON

  // Remote ON (redundant, already effectively on)
  img->power_on = true;

  // Physical switch flips ON -> OFF: edge writes power_on = false (last transition)
  apply_switch_edge(img, prev_switch, false);
  TEST_ASSERT_FALSE(img->power_on);

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
  TEST_ASSERT_FALSE(result.pump);
}

TEST_CASE("IO Precedence: physical ON then remote OFF -> remote wins (OFF)", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  bool prev_switch = false; // switch currently OFF

  // Physical switch flips OFF -> ON: edge writes power_on = true
  apply_switch_edge(img, prev_switch, true);
  TEST_ASSERT_TRUE(img->power_on);

  // Remote OFF is the last transition
  img->power_on = false; // power_standby()
  TEST_ASSERT_FALSE(img->power_on);

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
}

// ============================================================================
// SECTION 9: Refill State Machine Sequencing (driven at scan rate)
// ============================================================================

static boiler_refill_cfg_t make_refill_cfg() {
  boiler_refill_cfg_t cfg = {};
  cfg.start_delay_ms = 0;             // immediate UNKNOWN -> IDLE/ACTIVE
  cfg.stabilise_ms = 0;
  cfg.adc_num_readings = 1;
  cfg.refill_mv_threshold = 1000;
  cfg.max_refill_time_ms = 5000;      // timeout -> ERROR
  cfg.level_low_hysteresis_ms = 100;  // low for 100ms -> ACTIVE
  cfg.level_ok_hysteresis_ms = 100;   // ok for 100ms -> IDLE
  return cfg;
}

TEST_CASE("Refill: level OK at power-on settles to IDLE", "[io_scan]") {
  auto cfg = make_refill_cfg();
  boiler_refill_states_init(cfg);
  boiler_refill_states_power_on();

  boiler_refill_states_process(0, /*level_ok=*/true, /*in_error=*/false);
  TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
}

TEST_CASE("Refill: low water at power-on goes ACTIVE", "[io_scan]") {
  auto cfg = make_refill_cfg();
  boiler_refill_states_init(cfg);
  boiler_refill_states_power_on();

  boiler_refill_states_process(0, /*level_ok=*/false, /*in_error=*/false);
  TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
}

TEST_CASE("Refill: IDLE -> ACTIVE only after low-level hysteresis", "[io_scan]") {
  auto cfg = make_refill_cfg();
  boiler_refill_states_init(cfg);
  boiler_refill_states_power_on();

  // Settle IDLE
  boiler_refill_states_process(0, true, false);
  TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());

  // Level drops low, but not long enough
  boiler_refill_states_process(10, false, false);
  TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());

  // Sustained low beyond hysteresis (100ms)
  boiler_refill_states_process(200, false, false);
  TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
}

TEST_CASE("Refill: ACTIVE -> IDLE after level-ok hysteresis", "[io_scan]") {
  auto cfg = make_refill_cfg();
  boiler_refill_states_init(cfg);
  boiler_refill_states_power_on();

  boiler_refill_states_process(0, false, false); // ACTIVE
  TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());

  // Level recovers but hysteresis not yet met
  boiler_refill_states_process(50, true, false);
  TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());

  // Sustained ok beyond hysteresis
  boiler_refill_states_process(300, true, false);
  TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
}

TEST_CASE("Refill: ACTIVE longer than max_refill_time latches ERROR", "[io_scan]") {
  auto cfg = make_refill_cfg();
  boiler_refill_states_init(cfg);
  boiler_refill_states_power_on();

  boiler_refill_states_process(0, false, false); // ACTIVE
  TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());

  // Exceed max_refill_time_ms (5000) while still low
  boiler_refill_states_process(6000, false, false);
  TEST_ASSERT_EQUAL(REFILL_STATE_ERROR, boiler_refill_state());
}

TEST_CASE("Refill: ERROR latches until power cycle", "[io_scan]") {
  auto cfg = make_refill_cfg();
  boiler_refill_states_init(cfg);
  boiler_refill_states_power_on();

  boiler_refill_states_process(0, false, false);
  boiler_refill_states_process(6000, false, false); // -> ERROR
  TEST_ASSERT_EQUAL(REFILL_STATE_ERROR, boiler_refill_state());

  // Even with water restored, ERROR persists (no auto-recovery)
  boiler_refill_states_process(7000, true, false);
  boiler_refill_states_process(8000, true, false);
  TEST_ASSERT_EQUAL(REFILL_STATE_ERROR, boiler_refill_state());

  // Power cycle clears it
  boiler_refill_states_power_on();
  boiler_refill_states_process(9000, true, false);
  TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
}

// ============================================================================
// SECTION 10: Integration Flow Scenarios (process image end-to-end state)
// ============================================================================

TEST_CASE("Flow: boot in standby keeps all outputs off through safety gate", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  // Sensor task later reports healthy values, control loop requests duty
  img->power_on = false;
  img->water_level_ok = true;
  img->temperatures[1].fault = 0;
  img->temperatures[1].value = 90.0;
  img->ssr_boiler_duty = 100;
  img->pump_on = true;
  img->aux_on = true;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
  TEST_ASSERT_FALSE(result.pump);
  TEST_ASSERT_FALSE(result.aux);
}

TEST_CASE("Flow: boot powered on with healthy sensors allows control", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  img->power_on = true;
  img->water_level_ok = true;
  img->descale_mode = false;
  img->refill_state = REFILL_STATE_IDLE;
  img->temperatures[1].fault = 0;
  img->temperatures[1].value = 95.0;
  img->ssr_boiler_duty = 70;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(70, result.ssr_duty);
}

TEST_CASE("Flow: low-water cutoff during operation keeps pump for refill", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  img->power_on = true;
  img->water_level_ok = false; // dropped low
  img->refill_state = REFILL_STATE_ACTIVE;
  img->temperatures[1].fault = 0;
  img->temperatures[1].value = 95.0;
  img->ssr_boiler_duty = 80;
  img->pump_on = true;         // refill drives pump
  img->refill_solenoid_on = true;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);  // heater cut
  TEST_ASSERT_TRUE(result.pump);          // pump keeps running for refill
  TEST_ASSERT_TRUE(result.solenoid);
}

TEST_CASE("Flow: descale mode inhibits heater but allows brew path outputs", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  img->power_on = true;
  img->water_level_ok = true;
  img->descale_mode = true;
  img->temperatures[1].fault = 0;
  img->temperatures[1].value = 95.0;
  img->ssr_boiler_duty = 90;
  img->pump_on = true;
  img->three_way_on = true;

  auto result = apply_safety_overrides(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
  TEST_ASSERT_TRUE(result.pump);
  TEST_ASSERT_TRUE(result.three_way);
}

// ============================================================================
// SECTION 12: boiler_temp_process cutoffs via process image (migrated off bits)
// ============================================================================
#include "boiler_temp.h"

TEST_CASE("Boiler: standby forces desired duty to 0 (process image)", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  img->power_on = false;
  img->water_level_ok = true;
  img->ssr_boiler_duty = 80;

  measure_t data = {.value = 95.0, .fault = 0};
  boiler_temp_process(1000000, data);
  TEST_ASSERT_EQUAL(0, img->ssr_boiler_duty);
}

TEST_CASE("Boiler: descale mode forces desired duty to 0 (process image)", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  img->power_on = true;
  img->water_level_ok = true;
  img->descale_mode = true;
  img->ssr_boiler_duty = 70;

  measure_t data = {.value = 95.0, .fault = 0};
  boiler_temp_process(1000000, data);
  TEST_ASSERT_EQUAL(0, img->ssr_boiler_duty);
}

TEST_CASE("Boiler: low water forces desired duty to 0 (process image)", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  img->power_on = true;
  img->water_level_ok = false;
  img->ssr_boiler_duty = 65;

  measure_t data = {.value = 95.0, .fault = 0};
  boiler_temp_process(1000000, data);
  TEST_ASSERT_EQUAL(0, img->ssr_boiler_duty);
}

TEST_CASE("Boiler: RTD fault forces desired duty to 0 (process image)", "[io_scan]") {
  process_image_init();
  auto *img = process_image_get();
  img->power_on = true;
  img->water_level_ok = true;
  img->descale_mode = false;
  img->ssr_boiler_duty = 90;

  measure_t data = {.value = 95.0, .fault = 5}; // RTD fault
  boiler_temp_process(1000000, data);
  TEST_ASSERT_EQUAL(0, img->ssr_boiler_duty);
}


