#pragma once

#include <ctime>
#include "hw/rtds.h"
#include <hw_config.h>

#define BOILER_SSR_PIN GPIO_TRIG1_SSR

// Variable time base PID
void boiler_temp_init();

void boiler_temp_delete();

void boiler_temp_tick(uint64_t time_us, const rtd_data_t& data);

/**
 * Disables/Enables power to the boiler SSR.
 *
 * @param enable Stops feeding power to the boiler immediately when set to false
 */
void boiler_temp_enable(bool enable);

bool boiler_temp_is_enabled();
