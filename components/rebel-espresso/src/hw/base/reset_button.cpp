#include <hal/gpio_types.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include "reset_button.h"

static bool _trigger_reset = false;

IRAM_ATTR static void _handler(void*) {
  _trigger_reset = true;
}

bool reset_button_is_triggered() {
  return _trigger_reset;
}

void reset_button_init(gpio_num_t gpio) {
  gpio_isr_handler_add(gpio, _handler, nullptr);

  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_LOW_LEVEL;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (
      (1ULL << gpio)
  );

  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);
}