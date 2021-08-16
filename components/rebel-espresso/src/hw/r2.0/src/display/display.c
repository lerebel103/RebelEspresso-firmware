#include "display.h"

#include <hal/gpio_types.h>

#include "hw_config.h"

static esp_event_loop_handle_t s_event_loop;

void display_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;

    // Initialise pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << PIN_OUT_DISPLAY_CS) |
            (1ULL << PIN_OUT_DISPLAY_DC) |
            (1ULL << PIN_OUT_DISPLAY_RESET) |
            (1ULL << PIN_OUT_DISPLAY_LED)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
}