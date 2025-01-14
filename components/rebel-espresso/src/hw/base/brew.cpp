#include <freertos/FreeRTOS.h>

#include <esp_check.h>
#include <hal/gpio_types.h>
#include <hw_config.h>
#include <src/events.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <nvs_handle.hpp>
#include "brew.h"
#include "out_signals.h"
#include "hw_specs.h"
#include "boiler_refill.h"
#include "sys/nvram_store.h"

#define TAG "brew"
#define BREW_REFILL_NVS_STATUS_STORE     "st.brew"
#define KEY_brew_count                   "brew_cnt"
#define KEY_descale_count                "descale_cnt"
#define KEY_descale_last                 "descale_last"


static bool s_pump_sw_on = false;
static bool s_boiler_refilling = false;
static bool s_power_on = false;
static brew_status_t s_status = {1500, 0, 0};
static uint64_t s_brew_start_time = 0;

static TaskHandle_t brew_task_handle = nullptr;



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

static void _load_nvram() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(BREW_REFILL_NVS_STATUS_STORE, NVS_READWRITE, &my_handle));

  nvs_get_u32(my_handle, KEY_brew_count, &s_status.brew_count);
  nvs_get_u32(my_handle, KEY_descale_count, &s_status.descale_count);
  nvs_get_i64(my_handle, KEY_descale_last, (int64_t*)&s_status.last_descale_time);

  nvs_close(my_handle);
}

static void _save_nvram() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(BREW_REFILL_NVS_STATUS_STORE, NVS_READWRITE, &my_handle));

  nvs_set_u32(my_handle, KEY_brew_count, s_status.brew_count);
  nvs_set_u32(my_handle, KEY_descale_count, s_status.descale_count);
  nvs_set_i64(my_handle, KEY_descale_last, (int64_t)s_status.last_descale_time);

  nvs_close(my_handle);
}


static void _refill_events(
    [[maybe_unused]] void *handler_args,
    [[maybe_unused]] esp_event_base_t base, int32_t id,
    [[maybe_unused]] void *event_data) {
  if (id == BOILER_REFILL_STARTED) {
    s_boiler_refilling = true;
    ESP_LOGI(TAG, "Refill started, turning pump on");
    _pump_on();
  } else if (id == BOILER_REFILL_STOPPED || id == BOILER_REFILL_ERROR) {
    s_boiler_refilling = false;
    if (!s_pump_sw_on) {
      ESP_LOGI(TAG, "Refill stopped, turning off pump");
      _pump_off();
    }
  }
}

static void _brew_switch_off() {
  bool is_on = s_pump_sw_on;
  s_pump_sw_on = false;

  if (is_on) {
    if (!s_boiler_refilling) {
      _pump_off();
    }

    _three_way_valve_off();

    // Only send end event if switch was previously on
    auto now_us = esp_timer_get_time();
    ESP_ERROR_CHECK(esp_event_post(MACHINE_EVENTS, BREW_STOPPED, (void *) &now_us, 0,
                                   portMAX_DELAY));

    // Record as brew event if we have brewed something for some time
    if (!(xEventGroupGetBits(status_event_group) & DESCALE_MODE_BIT) &&
       (now_us - s_brew_start_time) > (uint64_t)(10*1e6)) {
      s_status.brew_count++;
      _save_nvram();
    }
  }
}

static void _brew_switch_on() {
  // Not running any of this in standby
  if (!s_power_on) {
    return;
  }

  bool is_on = s_pump_sw_on;
  s_pump_sw_on = true;

  if (!is_on) {
    _three_way_valve_on();
    _pump_on();

    // notify brew started
    auto now_us = esp_timer_get_time();
    s_brew_start_time = now_us;
    ESP_ERROR_CHECK(esp_event_post(MACHINE_EVENTS, BREW_STARTED, (void *) &now_us, 0,
                                   portMAX_DELAY));
  }
}

static void IRAM_ATTR _handler(void *) {
  // Notify task to handle state change
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xTaskNotifyFromISR(brew_task_handle, 0, eNoAction, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


/* Task that monitors the brew switch state and takes appropriate actions outside an ISR context */
[[noreturn]] void monitor_brew(void *) {
  uint64_t last_monitor_brew_us = 0;
  auto debounce_ms = 20;
  auto monitor_ms = 100;
  bool last_state = false;

  do {
    // Wait for notification from ISR
    auto timeout = (s_power_on ? pdMS_TO_TICKS(monitor_ms) : portMAX_DELAY);
    ulTaskNotifyTake(pdTRUE, timeout);

    // Don't process if we are in standby
    if (!s_power_on) {
      continue;
    }

    // Debounce
    auto last = last_monitor_brew_us;
    auto now_us = esp_timer_get_time();
    last_monitor_brew_us = now_us;
    if (now_us - last < debounce_ms * 1000) {
      continue;
    }

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
  } while (true);
}

static void _power_events(
    [[maybe_unused]] void *handler_args,
    [[maybe_unused]] esp_event_base_t base,
    int32_t id,
    [[maybe_unused]] void *event_data) {
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

      s_status.descale_count++;
      s_status.last_descale_time = time(nullptr);
      _save_nvram();
    }

    s_power_on = true;
  }
}

brew_status_t brew_get_status() {
  return s_status;
}

void brew_init() {
  [[maybe_unused]] esp_err_t ret = ESP_OK;
  ESP_GOTO_ON_FALSE(brew_task_handle == nullptr, ESP_ERR_INVALID_STATE, err, TAG, "Brew module already initialised");

  _load_nvram();

  // Ensure all is low state.
  _pump_off();
  _three_way_valve_off();

  // Create task to monitor brew switch state
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

  err:
    // Nothing to do
}

