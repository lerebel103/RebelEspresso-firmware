#include <freertos/FreeRTOS.h>

#include <hal/gpio_types.h>
#include <hw_config.h>
#include <src/events.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include "brew.h"
#include "out_signals.h"
#include "hw_specs.h"
#include "boiler_refill.h"

#define TAG "brew"

static bool s_go = true;
static bool _pump_sw_on = false;
static bool _boiler_refilling = false;

static bool s_power_on = false;
static TaskHandle_t brew_task_handle;


static void _pump_on() {
  out_signals_set_level(OUT_SIGNALS_RELAY1, 1);
}

static void _pump_off() {
  out_signals_set_level(OUT_SIGNALS_RELAY1, 0);
}

static void _three_way_valve_on() {
  out_signals_set_level(OUT_SIGNALS_RELAY3, 1);
}

static void _three_way_valve_off() {
  out_signals_set_level(OUT_SIGNALS_RELAY3, 0);
}

static void _refill_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
  if (id == BOILER_REFILL_STARTED) {
    _boiler_refilling = true;
    ESP_LOGI(TAG, "Refill started, turning pump on");
    _pump_on();
  } else if (id == BOILER_REFILL_STOPPED || id == BOILER_REFILL_ERROR) {
    _boiler_refilling = false;
    if (!_pump_sw_on) {
      ESP_LOGI(TAG, "Refill stopped, turning off pump");
      _pump_off();
    }
  }
}

static void _brew_switch_on() {
  // Not running any of this in standby
  if (!s_power_on) {
    return;
  }

  bool is_on = _pump_sw_on;
  _pump_sw_on = true;

  if (!is_on) {
    _three_way_valve_on();
    _pump_on();

    // notify brew started
    auto now_us = esp_timer_get_time();
    ESP_ERROR_CHECK(esp_event_post(MACHINE_EVENTS, BREW_STARTED, (void *) &now_us, 0,
                                   portMAX_DELAY));
  }
}

static void _brew_switch_off() {
  bool is_on = _pump_sw_on;
  _pump_sw_on = false;

  if (!_boiler_refilling) {
    _pump_off();
  }

  if (is_on) {
    _three_way_valve_off();

    // Only send end event if switch was previously on
    auto now_us = esp_timer_get_time();
    ESP_ERROR_CHECK(esp_event_post(MACHINE_EVENTS, BREW_STOPPED, (void *) &now_us, 0,
                                   portMAX_DELAY));
  }
}

static void IRAM_ATTR _handler(void *) {
  // Notify task to handle state change
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xTaskNotifyFromISR(brew_task_handle, 0, eNoAction, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}


/* Task that monitors the brew switch state and takes appropriate actions outside an ISR context */
void monitor_brew(void *) {
  uint64_t last_monitor_brew_us = 0;
  auto debounce_ms = 50;
  bool last_state = false;

  do {
    // Wait for notification from ISR
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(debounce_ms));

    // Debounce
    auto now_us = esp_timer_get_time();
    if (now_us - last_monitor_brew_us < debounce_ms * 1000) {
      continue;
    }
    last_monitor_brew_us = now_us;

    // Don't process if we have the same state
    bool state = gpio_get_level(PIN_IN_BREW_EN) == 0;
    if (state != last_state) {
    last_state = state;

    if (state) {
      _brew_switch_on();
    } else {
      _brew_switch_off();
    }
    }
  } while (s_go);

  vTaskDelete(nullptr);
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
  if (id == POWER_STANDBY) {
    s_power_on = false;

    // Always stop pump regardless
    _pump_off();
    _three_way_valve_off();

    // Clear off descale mode
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);
  } else if (id == POWER_ACTIVE) {
    // If pump switch is on when active, we enter descaling mode
    if (gpio_get_level(PIN_IN_BREW_EN) == 0) {
      xEventGroupSetBits(status_event_group, DESCALE_MODE_BIT);
    }

    s_power_on = true;
  }
}


void brew_init() {
  s_power_on = false;

  // Ensure all is low state.
  _brew_switch_off();

  // Create task to monitor brew switch state
  s_go = true;
  xTaskCreate(monitor_brew, "monitor_brew", 2560, nullptr, 6, &brew_task_handle);

  // --- Configure input switch that drives power state
  gpio_isr_handler_add(PIN_IN_BREW_EN, _handler, nullptr);

  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_ANYEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = ((1ULL << PIN_IN_BREW_EN));
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);


  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, BOILER_REFILL_STARTED,
                                             _refill_events, nullptr));

  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, BOILER_REFILL_STOPPED,
                                             _refill_events, nullptr));

  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, BOILER_REFILL_ERROR,
                                             _refill_events, nullptr));

  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, POWER_STANDBY,
                                             _power_events, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, POWER_ACTIVE,
                                             _power_events, nullptr));
}

