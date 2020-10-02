#include <cstdint>
#include <hw/rtds.h>
#include <esp_log.h>
#include "brew_head.h"

#define TAG "BrewHead"

void brew_head_tick(uint64_t time_us, const rtd_data_t& data) {
    if (data.fault == Max31865Error::NoError) {
        // Good to go
        ESP_LOGI(TAG, "BrewHead Temp=%f", data.temperature);

    } else {
        ESP_LOGE(TAG, "BrewHead sensor error %s", Max31865::errorToString(data.fault));
    }
}

void brew_head_tec_hot_updated(uint64_t time_us, const rtd_data_t& data) {

}

void brew_head_tec_cold_updated(uint64_t time_us, const rtd_data_t& data) {

}


void brew_head_init() {

}
