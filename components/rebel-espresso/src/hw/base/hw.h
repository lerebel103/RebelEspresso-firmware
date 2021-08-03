#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint-gcc.h>
#include <esp_event_base.h>

void hw_init(esp_event_loop_handle_t event_loop);

#ifdef __cplusplus
}
#endif
