#pragma once

void brew_head_init();

void brew_head_tick(uint64_t time_us, const rtd_data_t& data);

/**
 * Just got a new value for hot side of TEC
 * @param time_us
 * @param data
 */
void brew_head_tec_hot_updated(uint64_t time_us, const rtd_data_t& data);
/**
 * Just got a new value for cold side of TEC
 */
void brew_head_tec_cold_updated(uint64_t time_us, const rtd_data_t& data);
