#pragma once

#include <src/utils/reading.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint-gcc.h>
#include <cJSON.h>

void hw_specs_init();

void hw_specs_cfg_to_json(cJSON *root, const char *base_key);
void hw_specs_status_to_json(cJSON *root, const char *base_key);

void hw_specs_handle_new_cfg(const cJSON *cfg);

void hw_specs_handle_new_temp(uint64_t time_us, const reading_t &data, uint8_t idx);

void hw_specs_read_water_level_mv(uint8_t *status, double *value);

/**
 * Depending on hardware revision (r2 and up), auxiliary input may be supported.
 * @return true if it is currently activated.
 */
bool hw_specs_is_aux_in_activated();

#ifdef __cplusplus
}
#endif
