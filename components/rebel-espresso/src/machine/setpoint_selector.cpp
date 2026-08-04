#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <src/events.h>
#include <esp_event.h>
#include <esp_log.h>
#include "setpoint_selector.h"
#include "boiler_temp.h"
#include "process_image.h"

#define TAG "setpoint_selector"

// Boiler PID setpoint index selected by the steam switch:
// 0 = brew/primary, 1 = steam/secondary.
int setpoint_selector_index(bool steam_on) {
  return steam_on ? 1 : 0;
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

  const process_image_t *img = process_image_get();

  // Not running any of this in standby
  if (!img->power_on) {
    return;
  }

  // The steam switch selects the boiler's secondary PID setpoint. Read the
  // debounced state from the process image (I/O scan owns the switch GPIOs).
  boiler_set_active_setpoint(setpoint_selector_index(img->steam_on));
}

void setpoint_selector_init() {
  // The steam switch GPIO is configured and debounced by the I/O scan; this
  // module only maps the process-image steam state onto the boiler setpoint.
  boiler_set_active_setpoint(setpoint_selector_index(process_image_get()->steam_on));

  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, TICK, _tick, nullptr));
}
