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

#ifdef __cplusplus
}
#endif
