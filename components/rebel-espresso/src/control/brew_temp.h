#pragma once

void brew_temp_init();

void brew_temp_tick(uint64_t time_us, const rtd_data_t& data);

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
