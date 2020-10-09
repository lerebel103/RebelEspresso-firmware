#pragma once

#include <ctime>
#include "hw/rtds.h"
#include <hw_config.h>
#include <esp_event_base.h>

#define BOILER_SSR_PIN GPIO_TRIG1_SSR

// Variable time base PID
void boiler_temp_init(esp_event_loop_handle_t event_loop);

void boiler_temp_delete();

void boiler_temp_tick(uint64_t time_us, const rtd_data_t& data);

