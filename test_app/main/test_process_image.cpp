/**
 * Process image tests.
 *
 * Verifies the shared state component that bridges all runtime layers:
 *   - safe boot defaults (outputs off, inputs inactive, sensors faulted)
 *   - field isolation between writer layers
 *   - the per-channel seqlock (process_image_read_temp/write_temp) that keeps
 *     the (value, fault) pair consistent for safety reads.
 */
#include <unity.h>

#include "process_image.h"
#include "io_scan_safety.h"
#include "boiler_refill_states.h"

// ============================================================================
// Boot defaults (safe state)
// ============================================================================

TEST_CASE("PI: init sets all outputs OFF", "[process_image]") {
  process_image_init();
  auto *img = process_image_get();

  TEST_ASSERT_EQUAL(0, img->ssr_boiler_duty);
  TEST_ASSERT_FALSE(img->pump_on);
  TEST_ASSERT_FALSE(img->refill_solenoid_on);
  TEST_ASSERT_FALSE(img->three_way_on);
  TEST_ASSERT_FALSE(img->aux_on);
}

TEST_CASE("PI: init sets all inputs inactive", "[process_image]") {
  process_image_init();
  auto *img = process_image_get();

  TEST_ASSERT_FALSE(img->power_on);
  TEST_ASSERT_FALSE(img->brew_on);
  TEST_ASSERT_FALSE(img->steam_on);
  TEST_ASSERT_FALSE(img->descale_mode);
}

TEST_CASE("PI: init marks sensors faulted (heater inhibited until valid read)", "[process_image]") {
  process_image_init();
  auto *img = process_image_get();

  // Every temperature channel must indicate fault (non-zero) at boot.
  for (int i = 0; i < PROCESS_IMAGE_MAX_SENSORS; i++) {
    TEST_ASSERT_NOT_EQUAL(0, img->temperatures[i].fault);
  }
  TEST_ASSERT_FALSE(img->water_level_ok);
}

// ============================================================================
// Field isolation between writer layers
// ============================================================================

TEST_CASE("PI: sensor writes don't affect output fields", "[process_image]") {
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

TEST_CASE("PI: input writes don't affect sensor fields", "[process_image]") {
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

// ============================================================================
// Seqlock temperature snapshot (process_image_read_temp/write_temp)
// ============================================================================

TEST_CASE("PI Seqlock: write then read returns consistent snapshot", "[process_image]") {
  process_image_init();

  measure_t w = {.value = 96.5, .fault = 0};
  process_image_write_temp(1, w);

  measure_t r = process_image_read_temp(1);
  TEST_ASSERT_EQUAL_DOUBLE(96.5, r.value);
  TEST_ASSERT_EQUAL(0, r.fault);
}

TEST_CASE("PI Seqlock: out-of-range index returns zeroed measure", "[process_image]") {
  process_image_init();
  measure_t r = process_image_read_temp(PROCESS_IMAGE_MAX_SENSORS);
  TEST_ASSERT_EQUAL_DOUBLE(0.0, r.value);
  TEST_ASSERT_EQUAL(0, r.fault);
}

TEST_CASE("PI Seqlock: safety gate reads boiler temp through snapshot", "[process_image]") {
  process_image_init();
  auto *img = process_image_get();
  img->power_on = true;
  img->water_level_ok = true;
  img->refill_state = REFILL_STATE_IDLE;
  img->ssr_boiler_duty = 75;

  // Publish an over-temp boiler reading via the seqlock writer.
  measure_t hot = {.value = 150.0, .fault = 0};
  process_image_write_temp(1, hot);

  auto result = io_scan_apply_safety(img);
  TEST_ASSERT_EQUAL(0, result.ssr_duty);
}
