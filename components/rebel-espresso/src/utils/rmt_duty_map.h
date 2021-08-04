#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <soc/rmt_struct.h>
#include "rmt_duty_map.h"

#define MAINS_50HZ 50
#define MAINS_60HZ 60

struct rmt_pulse_t {
    const rmt_item32_t *items;
    uint16_t num_items;
};

// This is where we get our RMT pulses from for each desired duty cycle value
extern const struct rmt_pulse_t rmt_cycle_duty_map_50hz[101];
extern const struct rmt_pulse_t rmt_cycle_duty_map_60hz[101];


/**
 * Translates a desired duty cycle into an RMT pulse sequencing
 *
 * The generates pulses are in groups of 3 active mains cycles to mitigate wear on the resistive element.
 * The generate period is therefore not fixed.
 *
 * @param duty integral value in range of [0, 100]
 * @return corresponding Pulses to achieve the desired duty.
 */
inline const struct rmt_pulse_t* rmt_duty_get_pulses(int duty, uint8_t mains_hz) {
    if (duty > 100) {
        duty = 100;
    } else if (duty < 0) {
        duty = 0;
    }

    if (mains_hz == MAINS_50HZ) {
        return &rmt_cycle_duty_map_50hz[duty];
    } else {
        return &rmt_cycle_duty_map_60hz[duty];
    }
}

#ifdef __cplusplus
}
#endif
