#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Water-probe diagnostic helpers — pure, no I/O, unit-testable.
 *
 * The probe voltage from a failing/corroding electrode is spiky, so the reported
 * diagnostic value is a rolling median (robust against intermittent glitches)
 * rather than a mean. This module owns only the window maths; the sensor task
 * feeds it samples and publishes the median into the process image.
 */

#define WATER_PROBE_WINDOW 15

typedef struct {
  uint16_t samples[WATER_PROBE_WINDOW];
  uint8_t count; ///< number of valid samples (< WATER_PROBE_WINDOW until filled)
  uint8_t head;  ///< next write position (ring buffer)
} water_probe_window_t;

#ifdef __cplusplus
extern "C" {
#endif

/// Clear the window (count = 0).
void water_probe_window_reset(water_probe_window_t *w);

/// Push a new probe voltage sample (mV), overwriting the oldest once full.
void water_probe_window_push(water_probe_window_t *w, uint16_t mv);

/// Median of the current samples in mV; 0 when empty. Even counts average the
/// two central values.
uint16_t water_probe_window_median(const water_probe_window_t *w);

/**
 * Corrosion status derived from the (wet) probe reading vs the calibrated
 * thresholds. Higher voltage = more corroded (rising contact resistance).
 */
typedef enum {
  CORROSION_OK = 0,
  CORROSION_SERVICE_SOON = 1, ///< reading >= warn threshold — advise servicing
  CORROSION_FAULT = 2,        ///< reading >= fault threshold — probe untrustworthy
} corrosion_status_t;

/// Debounced corrosion status tracker (escalates and auto-clears symmetrically).
typedef struct {
  corrosion_status_t status;
  corrosion_status_t pending;
  uint32_t pending_since_ms;
} corrosion_monitor_t;

/// Instantaneous classification. Zero thresholds (uncalibrated) => CORROSION_OK.
corrosion_status_t corrosion_classify(uint16_t reading_mv, uint16_t warn_mv, uint16_t fault_mv);

/// Reset the monitor to a healthy state.
void corrosion_monitor_reset(corrosion_monitor_t *m);

/// Debounced update — the classified target must persist for `consistency_ms`
/// (in either direction) before the committed status changes. Call only with a
/// *wet* reading. Returns the committed status.
corrosion_status_t corrosion_monitor_update(corrosion_monitor_t *m, uint16_t reading_mv, uint16_t warn_mv,
                                            uint16_t fault_mv, uint32_t now_ms, uint32_t consistency_ms);

#ifdef __cplusplus
}
#endif
