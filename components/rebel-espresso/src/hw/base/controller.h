#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint-gcc.h>
#include <esp_event_base.h>

struct cJSON;

struct controller_cfg_t {
  bool enabled = false;
};

void controller_init();

void controller_enter_loop();

#ifdef __cplusplus
}
#endif
