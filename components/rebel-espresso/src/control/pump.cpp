#include <hal/gpio_types.h>
#include <hw_config.h>
#include "pump.h"

static void IRAM_ATTR _brew_switch_state_changed(void* arg) {
    bool trigger = true;
    if (gpio_get_level(GPIO_SW1)) {
        trigger = false;
    }
    gpio_set_level(GPIO_TRIG2_REL1, trigger);
}

void pump_init() {
    // --- Output pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_TRIG2_REL1)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Turn off
    gpio_set_level(GPIO_TRIG2_REL1, 0);

    // --- Configure input switch that drives the pump
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_SW1)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_isr_handler_add(GPIO_SW1, _brew_switch_state_changed, NULL);
}
