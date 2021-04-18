#pragma once

#include <freertos/FreeRTOS.h>
#include <esp_event_base.h>


#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

void homekit_init(esp_event_loop_handle_t event_loop);

bool homekit_is_initialised();

void homekit_terminate();

