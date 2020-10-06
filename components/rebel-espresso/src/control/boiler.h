#pragma once

#include <ctime>
#include "hw/rtds.h"
#include <hw_config.h>

#define BOILER_SSR_PIN GPIO_TRIG2_REL2

// Variable time base PID
void boiler_init();

void boiler_delete();

void boiler_tick(uint64_t time_us, const rtd_data_t& data);

/**
 * Disables/Enables power to the boiler SSR.
 *
 * @param enable Stops feeding power to the boiler immediately when set to false
 */
void boiler_enable(bool enable);

bool boiler_is_enabled();
