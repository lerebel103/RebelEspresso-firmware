#pragma once

#include <freertos/FreeRTOS.h>
#include <esp_event_base.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

void homekit_init(esp_event_loop_handle_t event_loop);


#ifdef __cplusplus
}
#endif
