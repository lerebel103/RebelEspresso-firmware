#include "out_signals.h"
#include "hw_config.h"

#include <esp_log.h>
#include <driver/gpio.h>

#define TAG "out_signals"

void out_signals_set_level(enum out_signals_t slot, uint8_t level) {

    if (slot == OUT_SIGNALS_RELAY1) {
        gpio_set_level(PIN_OUT_REL1_EN, level);
    } else if (slot == OUT_SIGNALS_RELAY2) {
        gpio_set_level(PIN_OUT_REL2_EN, level);
    } else if (slot == OUT_SIGNALS_RELAY3) {
        gpio_set_level(PIN_OUT_REL3_EN, level);
    } else if (slot == OUT_SIGNALS_AUX) {
        // Unsupported
        ESP_LOGW(TAG, "Auxiliary pin not supported");
    } else {
        // Unsupported
        ESP_LOGE(TAG, "Slot %d not supported", slot);
    }
}

uint8_t out_signals_get_level(enum out_signals_t slot) {
    uint8_t val = 0;

    if (slot == OUT_SIGNALS_RELAY1) {
        val = (GPIO_REG_READ(GPIO_OUT_REG) >> PIN_OUT_REL1_EN) & 1U;
    } else if (slot == OUT_SIGNALS_RELAY2) {
        val = (GPIO_REG_READ(GPIO_OUT_REG) >> PIN_OUT_REL2_EN) & 1U;
    } else if (slot == OUT_SIGNALS_RELAY3) {
        val = (GPIO_REG_READ(GPIO_OUT_REG) >> PIN_OUT_REL3_EN) & 1U;
    } else if (slot == OUT_SIGNALS_AUX) {
        // Unsupported
        ESP_LOGW(TAG, "Auxiliary pin not supported");
    } else {
        // Unsupported
        ESP_LOGE(TAG, "Slot %d not supported", slot);
    }

    return val;
}

void out_signals_init() {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << PIN_OUT_REL1_EN) |
            (1ULL << PIN_OUT_REL2_EN) |
            (1ULL << PIN_OUT_REL3_EN)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

}
