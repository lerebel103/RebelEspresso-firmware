#pragma once

#include <Max31865.h>


struct rtds_cfg_t {
};

struct rtd_data_t {
    double temperature;
    Max31865Error fault;
};

typedef void (*rtd_update_cb_t)(uint64_t time_us, const rtd_data_t& data, uint8_t rtd_idx);

int rtds_init(const rtds_cfg_t* cfg);

/**
 * Causes a new read of all RTDS, invokes callbacks and caches values.
 *
 * These can be read leater via #rtds_get()
 */
void rtds_update(rtd_update_cb_t cb);

/**
 * Retrieves a specific RTD value and state
 * @param data
 * @param idx
 */
esp_err_t rtds_get(rtd_data_t* data, uint8_t idx);
