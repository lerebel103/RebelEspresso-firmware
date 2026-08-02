#pragma once

#include <freertos/event_groups.h>
#include <esp_event_base.h>
#include <common/events_common.h>

extern EventGroupHandle_t status_event_group;

#define START_BIT MAX_ESP32_CONNECTIVITY_EVENTS_BIT
#define POWER_ON_BIT (START_BIT << 1)
#define BOILER_LEVEL_OK_BIT (START_BIT << 2)
#define DESCALE_MODE_BIT (START_BIT << 3)

ESP_EVENT_DECLARE_BASE(BUTTONS_EVENTS);

enum button_events_t {
  BUTTON_CONTROLLER_PRESSED,
  BUTTON_CONTROLLER_HELD,
  BUTTON_UP_PRESSED,
  BUTTON_DOWN_PRESSED,
};

ESP_EVENT_DECLARE_BASE(MACHINE_EVENTS);

enum machine_events_t {
  POWER_STANDBY,
  POWER_ACTIVE,
  TICK,
  BREW_STARTED,
  BREW_STOPPED,
  BOILER_REFILL_STARTED,
  BOILER_REFILL_STOPPED,
  BOILER_REFILL_ERROR
};