#pragma once

#include <Max31865.h>


struct rtds_cfg_t {
};

struct rtd_data_t {
    double temperature;
    Max31865Error fault;
};

int rtds_init(const rtds_cfg_t* cfg);

/**
 * Causes a new read of all RTDS and caches them.
 *
 * These can be read leater via #rtds_get()
 */
void rtds_update();

/**
 * Retrieves a specific RTD value and state
 * @param data
 * @param idx
 */
esp_err_t rtds_get(rtd_data_t* data, uint8_t idx);
