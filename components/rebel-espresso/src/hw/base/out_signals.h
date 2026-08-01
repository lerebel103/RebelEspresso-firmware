#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint-gcc.h>

enum out_signals_t {
  OUT_SIGNALS_RELAY1,
  OUT_SIGNALS_RELAY2,
  OUT_SIGNALS_RELAY3,
  OUT_SIGNALS_AUX,
};

void out_signals_set_level(enum out_signals_t slot, uint8_t level);

uint8_t out_signals_get_level(enum out_signals_t slot);

void out_signals_init();

#ifdef __cplusplus
}
#endif
