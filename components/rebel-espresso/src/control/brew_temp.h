#pragma once

#include <esp_event_base.h>

void brew_temp_init(esp_event_loop_handle_t event_loop);

void brew_temp_process(uint64_t time_us, const rtd_data_t& data);

/**
 * Just got a new value for hot side of TEC
 * @param time_us
 * @param data
 */
void brew_temp_tec_hot_updated(uint64_t time_us, const rtd_data_t& data);
/**
 * Just got a new value for cold side of TEC
 */
void brew_temp_tec_cold_updated(uint64_t time_us, const rtd_data_t& data);
