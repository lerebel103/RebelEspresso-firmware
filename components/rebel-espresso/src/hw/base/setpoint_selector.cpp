#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <hal/gpio_types.h>
#include <hw_config.h>
#include <src/events.h>
#include <esp_event.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "setpoint_selector.h"
#include "boiler_temp.h"

#define TAG "setpoint_selector"

static bool _setpoint_selector_sw_on = false;
static bool _boiler_refilling = false;

static void _setpoint_selector_on() {
  boiler_set_active_setpoint(1);
}

static void _setpoint_selector_off() {
  boiler_set_active_setpoint(0);
}

static void _selector_switch_off(void *arg) {
  _setpoint_selector_sw_on = false;
  if (!_boiler_refilling) {
    _setpoint_selector_off();
  }
}

/*
 * It's annoying to have to do this.
 * Unfortunately we can't get the edge state accurately if positive and negative
 * interrupt is selected, which makes it unreliable to drive the setpoint_selector from a single
 * configured ISR handler. So we need this secondary tick thing to ensure states
 * are consistent.
 */
static void _tick(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
  if (id != TICK) {
    return;
  }

  // Not running any of this in standby
  if (!(xEventGroupGetBits(status_event_group) & POWER_ON_BIT)) {
    return;
  }

  // maintain setpoint_selector state with switch
  if (gpio_get_level(PIN_IN_STEAM_EN) == 0) {
    _setpoint_selector_sw_on = true;
    _setpoint_selector_on();
  } else {
    _selector_switch_off(nullptr);
  }
}

void setpoint_selector_init() {
  gpio_config_t io_conf;

  // --- Configure input switch that drives the setpoint_selector
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = ((1ULL << PIN_IN_STEAM_EN));

  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  // Get everything synced up
  _tick(NULL, MACHINE_EVENTS, TICK, NULL);

  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, TICK, _tick, nullptr));
}
