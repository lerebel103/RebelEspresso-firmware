#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

enum Actuate_State_t {
    ACTUATE_STATE_OPENED,
    ACTUATE_STATE_CLOSED,
    ACTUATE_STATE_OPENING,
    ACTUATE_STATE_CLOSING,
    ACTUATE_STATE_STOPPED,
    ACTUATE_STATE_OBSTRUCTION,
    ACTUATE_STATE_ERROR,
};

void actuate_init();

Actuate_State_t actuate_get_state();

void actuate_blip_switch();

void actuate_tick(TickType_t millis);
