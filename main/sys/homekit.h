#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

void homekit_init();

void homekit_tick(TickType_t timestamp);