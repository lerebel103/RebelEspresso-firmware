#pragma once

#include <freertos/FreeRTOS.h>
#include <esp_event_base.h>


#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

void homekit_init();

bool homekit_is_initialised();

void homekit_terminate();

