#pragma once

#include <hal/spi_types.h>
#include "reading.h"

struct rtds_cfg_t {
};

// Error codes
#define RTD_NoError     0
#define RTD_Voltage     2
#define RTD_InLow       3
#define RTD_RefLow      4
#define RTD_RefHigh     5
#define RTD_RTDLow      6
#define RTD_RTDHigh     7

enum units_enum_t {
    UNIT_CELCIUS,
    UNIT_FARENHEIGHT
};

typedef void (*rtd_update_cb_t)(uint64_t time_us, const struct reading_t& data, uint8_t rtd_idx);

int rtds_init(spi_host_device_t spi, const struct rtds_cfg_t* cfg);

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
esp_err_t rtds_get(struct reading_t* data, uint8_t idx);

inline units_enum_t rtds_get_unit() {
    return UNIT_CELCIUS;
}