#include "out_signals.h"

#include <esp_log.h>

#define TAG "out_signals"

void out_signals_set_level(enum out_signals_t slot, uint8_t level) {

    if (slot == OUT_SIGNALS_RELAY1) {

    } else if (slot == OUT_SIGNALS_RELAY2) {

    } else if (slot == OUT_SIGNALS_RELAY3) {

    } else if (slot == OUT_SIGNALS_AUX) {

    } else {
        // Unsupported
        ESP_LOGE(TAG, "Slot %d not supported", slot);
    }
}

uint8_t out_signals_get_level(enum out_signals_t slot) {
    uint8_t val = 0;

    if (slot == OUT_SIGNALS_RELAY1) {

    } else if (slot == OUT_SIGNALS_RELAY2) {

    } else if (slot == OUT_SIGNALS_RELAY3) {

    } else if (slot == OUT_SIGNALS_AUX) {

    } else {
        // Unsupported
        ESP_LOGE(TAG, "Slot %d not supported", slot);
    }

    return val;
}

void out_signals_init() {

}

