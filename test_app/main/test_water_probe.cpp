/**
 * Water-probe diagnostic tests — pure rolling-median window used to produce a
 * glitch-robust probe voltage for corrosion monitoring.
 */
#include <unity.h>

#include "water_probe.h"

TEST_CASE("Probe: empty window median is 0", "[water_probe]") {
  water_probe_window_t w;
  water_probe_window_reset(&w);
  TEST_ASSERT_EQUAL_UINT16(0, water_probe_window_median(&w));
}

TEST_CASE("Probe: single sample median is that sample", "[water_probe]") {
  water_probe_window_t w;
  water_probe_window_reset(&w);
  water_probe_window_push(&w, 2400);
  TEST_ASSERT_EQUAL_UINT16(2400, water_probe_window_median(&w));
}

TEST_CASE("Probe: odd count returns the middle value", "[water_probe]") {
  water_probe_window_t w;
  water_probe_window_reset(&w);
  water_probe_window_push(&w, 300);
  water_probe_window_push(&w, 100);
  water_probe_window_push(&w, 200);
  TEST_ASSERT_EQUAL_UINT16(200, water_probe_window_median(&w));
}

TEST_CASE("Probe: even count averages the two central values", "[water_probe]") {
  water_probe_window_t w;
  water_probe_window_reset(&w);
  water_probe_window_push(&w, 100);
  water_probe_window_push(&w, 200);
  water_probe_window_push(&w, 300);
  water_probe_window_push(&w, 400);
  TEST_ASSERT_EQUAL_UINT16(250, water_probe_window_median(&w)); // (200+300)/2
}

TEST_CASE("Probe: median rejects intermittent glitch spikes", "[water_probe]") {
  water_probe_window_t w;
  water_probe_window_reset(&w);
  // Steady ~500 mV with occasional corrosion glitches to the rail.
  const uint16_t stream[] = {500, 505, 60000, 498, 502, 60000, 495, 503, 501};
  for (size_t i = 0; i < sizeof(stream) / sizeof(stream[0]); i++) {
    water_probe_window_push(&w, stream[i]);
  }
  uint16_t med = water_probe_window_median(&w);
  TEST_ASSERT_TRUE(med > 450 && med < 550); // glitches excluded
}

TEST_CASE("Probe: window is bounded and tracks the most recent samples", "[water_probe]") {
  water_probe_window_t w;
  water_probe_window_reset(&w);

  // Overfill the window with a low baseline, then flood with a higher one.
  for (int i = 0; i < WATER_PROBE_WINDOW; i++) {
    water_probe_window_push(&w, 100);
  }
  TEST_ASSERT_EQUAL_UINT16(100, water_probe_window_median(&w));

  for (int i = 0; i < WATER_PROBE_WINDOW; i++) {
    water_probe_window_push(&w, 900);
  }
  TEST_ASSERT_EQUAL_UINT16(900, water_probe_window_median(&w)); // old samples aged out
  TEST_ASSERT_EQUAL_UINT8(WATER_PROBE_WINDOW, w.count);
}

// ============================================================================
// Corrosion status classification + debounce
// ============================================================================

TEST_CASE("Corrosion: classify by thresholds (higher mV = more corroded)", "[water_probe]") {
  // baseline 500, warn 900, fault 1300
  TEST_ASSERT_EQUAL(CORROSION_OK, corrosion_classify(500, 900, 1300));
  TEST_ASSERT_EQUAL(CORROSION_OK, corrosion_classify(899, 900, 1300));
  TEST_ASSERT_EQUAL(CORROSION_SERVICE_SOON, corrosion_classify(900, 900, 1300));
  TEST_ASSERT_EQUAL(CORROSION_SERVICE_SOON, corrosion_classify(1299, 900, 1300));
  TEST_ASSERT_EQUAL(CORROSION_FAULT, corrosion_classify(1300, 900, 1300));
}

TEST_CASE("Corrosion: uncalibrated (zero thresholds) is always OK", "[water_probe]") {
  TEST_ASSERT_EQUAL(CORROSION_OK, corrosion_classify(60000, 0, 0));
}

TEST_CASE("Corrosion: escalation requires sustained consistency", "[water_probe]") {
  corrosion_monitor_t m;
  corrosion_monitor_reset(&m);
  const uint16_t warn = 900, fault = 1300, cons = 1000;

  // A brief spike above fault must NOT escalate before the consistency window.
  TEST_ASSERT_EQUAL(CORROSION_OK, corrosion_monitor_update(&m, 1400, warn, fault, 0, cons));
  TEST_ASSERT_EQUAL(CORROSION_OK, corrosion_monitor_update(&m, 500, warn, fault, 200, cons)); // recovered
  TEST_ASSERT_EQUAL(CORROSION_OK, corrosion_monitor_update(&m, 1400, warn, fault, 400, cons)); // restarts timer
  TEST_ASSERT_EQUAL(CORROSION_OK, corrosion_monitor_update(&m, 1400, warn, fault, 1200, cons)); // 800ms < 1000
  TEST_ASSERT_EQUAL(CORROSION_FAULT, corrosion_monitor_update(&m, 1400, warn, fault, 1500, cons)); // sustained
}

TEST_CASE("Corrosion: auto-clears after sustained recovery", "[water_probe]") {
  corrosion_monitor_t m;
  corrosion_monitor_reset(&m);
  const uint16_t warn = 900, fault = 1300, cons = 1000;

  // Drive into fault.
  corrosion_monitor_update(&m, 1400, warn, fault, 0, cons);
  TEST_ASSERT_EQUAL(CORROSION_FAULT, corrosion_monitor_update(&m, 1400, warn, fault, 1000, cons));

  // Recovery must also be sustained before clearing (auto-clear, symmetric).
  TEST_ASSERT_EQUAL(CORROSION_FAULT, corrosion_monitor_update(&m, 500, warn, fault, 1500, cons));
  TEST_ASSERT_EQUAL(CORROSION_OK, corrosion_monitor_update(&m, 500, warn, fault, 2600, cons));
}

