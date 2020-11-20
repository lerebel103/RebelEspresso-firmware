#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

void homekit_init();

void homekit_tick(TickType_t timestamp);

#ifdef __cplusplus
}
#endif
