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

corrosion_status_t corrosion_classify(uint16_t reading_mv, uint16_t warn_mv, uint16_t fault_mv) {
  if (fault_mv > 0 && reading_mv >= fault_mv) {
    return CORROSION_FAULT;
  }
  if (warn_mv > 0 && reading_mv >= warn_mv) {
    return CORROSION_SERVICE_SOON;
  }
  return CORROSION_OK;
}

void corrosion_monitor_reset(corrosion_monitor_t *m) {
  m->status = CORROSION_OK;
  m->pending = CORROSION_OK;
  m->pending_since_ms = 0;
}

corrosion_status_t corrosion_monitor_update(corrosion_monitor_t *m, uint16_t reading_mv, uint16_t warn_mv,
                                            uint16_t fault_mv, uint32_t now_ms, uint32_t consistency_ms) {
  corrosion_status_t target = corrosion_classify(reading_mv, warn_mv, fault_mv);

  if (target == m->status) {
    m->pending = m->status;
    m->pending_since_ms = now_ms;
  } else if (target != m->pending) {
    m->pending = target;
    m->pending_since_ms = now_ms;
  } else if ((now_ms - m->pending_since_ms) >= consistency_ms) {
    m->status = target;
    m->pending_since_ms = now_ms;
  }

  return m->status;
}
