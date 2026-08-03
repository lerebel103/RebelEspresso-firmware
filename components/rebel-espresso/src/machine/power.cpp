#include "power.h"

#include <src/events.h>
#include <hal/gpio_types.h>
#include <hw_config.h>
#include <esp_event.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "process_image.h"

/**
 * Power module — provides software-triggered power control for HomeKit/schedules.
 *
 * The physical power switch is now polled by the I/O scan task (20ms).
 * This module only provides the API for software-driven standby/active
 * transitions (e.g., HomeKit, wake schedules) and the status query.
 *
 * Remote vs physical precedence: these functions write process_image.power_on
 * directly. Together with the I/O scan's edge-driven switch handling they follow
 * a "last transition wins" rule — whichever source (a physical switch edge or a
 * remote command) changed power_on most recently determines the active state.
 * A remote command can therefore hold the machine on while the physical switch
 * is off, until the next physical switch edge.
 */

extern "C" void power_standby() {
  auto *img = process_image_get();
  img->power_on = false;
}

extern "C" void power_active() {
  auto *img = process_image_get();
  img->power_on = true;
}

extern "C" bool power_is_active() {
  return process_image_get()->power_on;
}

void power_init() {
  // Configure power switch GPIO as input (I/O scan reads it)
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = ((1ULL << PIN_IN_SYS_EN));
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);
}
