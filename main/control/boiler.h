#pragma once

#include <ctime>
#include "hw/rtds.h"

// Variable time base PID
void boiler_init();

void boiler_tick(uint64_t time_us, const rtd_data_t& data);
