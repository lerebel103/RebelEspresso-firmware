#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <esp_event_base.h>
#include "boiler_refill.h"


/**
 * Defines possible states that can be taken by the REFILL
 */
enum RefillState_t {
    REFILL_STATE_UNKNOWN,
    REFILL_STATE_STARTING,
    REFILL_STATE_IDLE,
    REFILL_STATE_ACTIVE,
    REFILL_STATE_ERROR
};



/**
 * String representation of the current state.
 */
static inline const char* state_to_str(RefillState_t state) {
    if (state == REFILL_STATE_ACTIVE) {
        return "REFILLING";
    } else if (state == REFILL_STATE_STARTING) {
        return "STARTING";
    } else if (state == REFILL_STATE_IDLE) {
        return "IDLE";
    } else if (state == REFILL_STATE_ERROR) {
        return "ERROR";
    } else {
        return "UNKNOWN";
    }
}

void boiler_refill_states_init(const boiler_refill_cfg_t& cfg);

void boiler_refill_states_process(uint64_t timestamp_ms, bool is_level_ok, bool in_error);

RefillState_t boiler_refill_state();

void boiler_refill_states_power_on();
void boiler_refill_states_power_standby();
