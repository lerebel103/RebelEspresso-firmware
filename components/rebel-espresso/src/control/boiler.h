#pragma once

#include <ctime>
#include "hw/rtds.h"

// Variable time base PID
void boiler_init();

void boiler_tick(uint64_t time_us, const rtd_data_t& data);

/**
 * Disables/Enables power to the boiler SSR.
 *
 * @param enable Stops feeding power to the boiler immediately when set to false
 */
void boiler_enable(bool enable);
