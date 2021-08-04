#include <src/hw/base/boiler_temp.h>
#include <src/hw/base/brew_temp.h>
#include "hw_specs.h"
#include "hw_config.h"


void hw_specs_init(esp_event_loop_handle_t event_loop) {
    // Initialise I2C bus

}

void hw_specs_cfg_to_json(cJSON *root, const char *base_key) {

}

void hw_specs_status_to_json(cJSON *root, const char *base_key) {

}

void hw_specs_handle_new_cfg(const cJSON *cfg) {

}

void hw_specs_handle_new_temp(uint64_t time_us, const reading_t &data, uint8_t idx) {
    switch (idx) {
        case RTD_BREW_BOILER_IDX:
            boiler_temp_process(time_us, data);
            break;
        case RTD_BREW_HEAD_IDX:
            brew_temp_process(time_us, data);
            break;
        case RTD_STEAM_BOILER_IDX:
            // Not yet implemented
            // steam_temp_process(time_us, data);
            break;
    }
}
