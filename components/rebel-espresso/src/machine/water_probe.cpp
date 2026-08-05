#include "water_probe.h"

void water_probe_window_reset(water_probe_window_t *w) {
  w->count = 0;
  w->head = 0;
  for (uint8_t i = 0; i < WATER_PROBE_WINDOW; i++) {
    w->samples[i] = 0;
  }
}

void water_probe_window_push(water_probe_window_t *w, uint16_t mv) {
  w->samples[w->head] = mv;
  w->head = (uint8_t)((w->head + 1) % WATER_PROBE_WINDOW);
  if (w->count < WATER_PROBE_WINDOW) {
    w->count++;
  }
}

uint16_t water_probe_window_median(const water_probe_window_t *w) {
  if (w->count == 0) {
    return 0;
  }

  uint16_t tmp[WATER_PROBE_WINDOW];
  for (uint8_t i = 0; i < w->count; i++) {
    tmp[i] = w->samples[i];
  }

  // Insertion sort — tiny window, no allocation.
  for (uint8_t i = 1; i < w->count; i++) {
    uint16_t key = tmp[i];
    int j = (int)i - 1;
    while (j >= 0 && tmp[j] > key) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = key;
  }

  uint8_t mid = (uint8_t)(w->count / 2);
  if (w->count & 1u) {
    return tmp[mid];
  }
  return (uint16_t)(((uint32_t)tmp[mid - 1] + tmp[mid]) / 2);
}
