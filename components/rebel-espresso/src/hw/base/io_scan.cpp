#include "io_scan.h"
#include "process_image.h"
#include "out_signals.h"
#include "boiler_temp.h"
#include "boiler_refill_states.h"
#include "boiler_refill.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>
#include <hw_config.h>
#include <src/events.h>
#include <esp_event.h>

#define TAG "io_scan"

// Scan interval in milliseconds
#define IO_SCAN_INTERVAL_MS 20

// Debounce: require N consecutive same-state reads before accepting.
// At 20ms scan rate, 2 reads = 40ms debounce window.
#define DEBOUNCE_COUNT 2

// Task handle
static TaskHandle_t s_task_handle = nullptr;
static bool s_running = false;

// Refill state tracking (used by both scan_inputs power transitions and scan_refill)
static RefillState_t s_prev_refill_state = REFILL_STATE_UNKNOWN;

// Power state tracking — detects changes from any source (GPIO edge or remote command)
static bool s_prev_power_on = false;

// ─── Debounce state per input ──────────────────────────────────────────────

struct debounce_t {
  bool stable_state; ///< Last accepted (debounced) state
  bool raw_state;    ///< Last raw GPIO reading
  uint8_t count;     ///< Consecutive readings matching raw_state
};

static debounce_t s_power_db = {false, false, 0};
static debounce_t s_brew_db = {false, false, 0};
static debounce_t s_steam_db = {false, false, 0};

/**
 * Update a debounce filter. Returns true if stable_state changed.
 */
static bool debounce_update(debounce_t& db, bool current_reading) {
  if (current_reading == db.raw_state) {
    if (db.count < DEBOUNCE_COUNT) {
      db.count++;
    }
  } else {
    db.raw_state = current_reading;
    db.count = 1;
  }

  if (db.count >= DEBOUNCE_COUNT && db.stable_state != db.raw_state) {
    db.stable_state = db.raw_state;
    return true; // State changed
  }
  return false;
}

// ─── Input reading ─────────────────────────────────────────────────────────

static void scan_inputs(process_image_t *img) {
  // All switch inputs are active-low (0 = ON, 1 = OFF)
  bool power_raw = gpio_get_level(PIN_IN_SYS_EN) == 0;
  bool brew_raw = gpio_get_level(PIN_IN_BREW_EN) == 0;
  bool steam_raw = gpio_get_level(PIN_IN_STEAM_EN) == 0;

  bool power_changed = debounce_update(s_power_db, power_raw);
  bool brew_changed = debounce_update(s_brew_db, brew_raw);
  debounce_update(s_steam_db, steam_raw);

  // Brew and steam: always reflect debounced GPIO level
  img->brew_on = s_brew_db.stable_state;
  img->steam_on = s_steam_db.stable_state;

  // Power: only update on GPIO edge (transition).
  // Between edges, the process image retains whatever was last set — either by
  // a GPIO transition or by a remote command (HomeKit/HTTP via power_active/power_standby).
  // This allows remote on/off to work even when the physical switch is in the opposite position.
  if (power_changed) {
    img->power_on = s_power_db.stable_state;
  }

  // Detect power state change from ANY source (GPIO edge or remote command)
  if (img->power_on != s_prev_power_on) {
    s_prev_power_on = img->power_on;

    if (img->power_on) {
      // Check if brew switch is held on power-on → descale mode
      if (s_brew_db.stable_state) {
        img->descale_mode = true;
      }
      // Restart refill state machine on power-on
      boiler_refill_states_power_on();
      s_prev_refill_state = REFILL_STATE_UNKNOWN;
      img->aux_on = true;
      ESP_LOGI(TAG, "Power ON%s", power_changed ? " (switch)" : " (remote)");
      esp_event_post(MACHINE_EVENTS, POWER_ACTIVE, nullptr, 0, 0);
    } else {
      img->descale_mode = false;
      // Stop refill on power-off
      boiler_refill_states_power_standby();
      img->refill_state = REFILL_STATE_UNKNOWN;
      img->refill_solenoid_on = false;
      img->aux_on = false;
      s_prev_refill_state = REFILL_STATE_UNKNOWN;
      ESP_LOGI(TAG, "Power OFF%s", power_changed ? " (switch)" : " (remote)");
      esp_event_post(MACHINE_EVENTS, POWER_STANDBY, nullptr, 0, 0);
    }
  }

  // Handle brew transitions
  if (brew_changed) {
    if (img->brew_on && img->power_on) {
      img->brew_active = true;
      img->brew_start_time_us = esp_timer_get_time();
      // Desired outputs: pump + 3-way ON
      img->pump_on = true;
      img->three_way_on = true;
      auto now_us = img->brew_start_time_us;
      esp_event_post(MACHINE_EVENTS, BREW_STARTED, (void *)&now_us, sizeof(now_us), 0);
      ESP_LOGI(TAG, "Brew STARTED");
    } else if (!img->brew_on && img->brew_active) {
      img->brew_active = false;
      // Desired outputs: pump + 3-way OFF (unless refill needs pump)
      if (!img->refill_solenoid_on) {
        img->pump_on = false;
      }
      img->three_way_on = false;
      auto now_us = esp_timer_get_time();
      esp_event_post(MACHINE_EVENTS, BREW_STOPPED, (void *)&now_us, sizeof(now_us), 0);
      ESP_LOGI(TAG, "Brew STOPPED");
    }
  }
}

// ─── Refill state machine (driven every scan cycle) ────────────────────────

static void scan_refill(process_image_t *img) {
  // Don't run refill logic in standby or descale
  if (!img->power_on || img->descale_mode) {
    return;
  }

  // Drive the state machine with current water level from process image
  uint64_t now_ms = esp_timer_get_time() / 1000;
  boiler_refill_states_process(now_ms, img->water_level_ok, false);

  RefillState_t state = boiler_refill_state();
  img->refill_state = state;

  // Set outputs based on state
  if (state == REFILL_STATE_ACTIVE) {
    img->refill_solenoid_on = true;
    // Pump on for refill (additive with brew)
    img->pump_on = true;
  } else {
    img->refill_solenoid_on = false;
    // Only turn pump off if brew isn't driving it
    if (!img->brew_active) {
      img->pump_on = false;
    }
  }

  // Post events on state transitions for Layer 4 consumers
  if (state != s_prev_refill_state) {
    if (state == REFILL_STATE_ACTIVE) {
      esp_event_post(MACHINE_EVENTS, BOILER_REFILL_STARTED, nullptr, 0, 0);
    } else if (s_prev_refill_state == REFILL_STATE_ACTIVE) {
      esp_event_post(MACHINE_EVENTS, BOILER_REFILL_STOPPED, nullptr, 0, 0);
    }
    if (state == REFILL_STATE_ERROR) {
      esp_event_post(MACHINE_EVENTS, BOILER_REFILL_ERROR, nullptr, 0, 0);
    }
    s_prev_refill_state = state;
  }
}

// ─── Safety overrides + output writing ─────────────────────────────────────

static void apply_outputs(process_image_t *img) {
  int ssr_duty = img->ssr_boiler_duty;
  bool pump = img->pump_on;
  bool solenoid = img->refill_solenoid_on;
  bool three_way = img->three_way_on;
  bool aux = img->aux_on;

  // ─── SAFETY OVERRIDES (non-negotiable) ───────────────────────────────
  if (!img->power_on) {
    // Standby: ALL outputs OFF
    ssr_duty = 0;
    pump = false;
    solenoid = false;
    three_way = false;
    aux = false;
  } else {
    // Power is on — apply conditional overrides
    if (!img->water_level_ok) {
      ssr_duty = 0;
    }
    if (img->refill_state == REFILL_STATE_ERROR) {
      ssr_duty = 0;
    }
    if (img->descale_mode) {
      ssr_duty = 0;
    }
    // Sensor fault on boiler RTD — cannot safely control heater
    if (img->temperatures[RTD_BREW_BOILER_IDX].fault != 0) {
      ssr_duty = 0;
    }
  }

  // ─── WRITE OUTPUTS TO HARDWARE (every cycle, unconditionally) ────────

  // SSR duty (applied via boiler_temp hardware interface, after safety overrides)
  boiler_temp_apply_hw_duty(ssr_duty);

  // I2C relays (pump, refill solenoid, 3-way valve, aux)
  out_signals_set_level(OUT_SIGNALS_RELAY1, pump ? 1 : 0);
  out_signals_set_level(OUT_SIGNALS_RELAY2, solenoid ? 1 : 0);
  out_signals_set_level(OUT_SIGNALS_RELAY3, three_way ? 1 : 0);
  out_signals_set_level(OUT_SIGNALS_AUX, aux ? 1 : 0);
}

// ─── Task loop ─────────────────────────────────────────────────────────────

static void io_scan_task(void *) {
  ESP_LOGI(TAG, "I/O scan task started (interval=%dms)", IO_SCAN_INTERVAL_MS);

  while (s_running) {
    auto *img = process_image_get();

    // 1. Read and debounce all switch inputs
    scan_inputs(img);

    // 2. Drive refill state machine
    scan_refill(img);

    // 3. Sync process image state to legacy event group bits
    //    (consumed by boiler_temp_process, brew_temp_process, display, homekit)
    if (img->power_on) {
      xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    } else {
      xEventGroupClearBits(status_event_group, POWER_ON_BIT);
    }
    if (img->water_level_ok) {
      xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    } else {
      xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);
    }
    if (img->descale_mode) {
      xEventGroupSetBits(status_event_group, DESCALE_MODE_BIT);
    } else {
      xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);
    }

    // 4. Apply safety overrides and write all outputs
    apply_outputs(img);

    // 5. Record scan timestamp
    img->last_scan_time_us = esp_timer_get_time();

    // 6. Reset watchdog
    esp_task_wdt_reset();

    // 7. Sleep until next cycle
    vTaskDelay(pdMS_TO_TICKS(IO_SCAN_INTERVAL_MS));
  }

  ESP_LOGI(TAG, "I/O scan task stopped");
  s_task_handle = nullptr;
  vTaskDelete(nullptr);
}

// ─── Public API ────────────────────────────────────────────────────────────

extern "C" void io_scan_init(void) {
  if (s_task_handle != nullptr) {
    ESP_LOGW(TAG, "Already initialised");
    return;
  }

  // Note: Switch GPIO inputs are already configured by power_init().
  // When legacy code is removed, pin configuration will move here exclusively.

  // Initialise refill state machine (config loaded by boiler_refill_init earlier)
  boiler_refill_states_init(boiler_refill_get_cfg());

  // Start task
  s_running = true;
  xTaskCreate(io_scan_task, "io_scan", 2048, nullptr, 8, &s_task_handle);

  // Enroll in existing task WDT (shared with control loop, 2s timeout).
  // If the I/O scan stalls for >2s, the system panics and restarts.
  esp_task_wdt_add(s_task_handle);

  ESP_LOGI(TAG, "Initialised");
}

extern "C" void io_scan_delete(void) {
  if (s_task_handle == nullptr) {
    return;
  }

  s_running = false;

  // Wait for task to exit
  while (s_task_handle != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(IO_SCAN_INTERVAL_MS * 2));
  }

  ESP_LOGI(TAG, "Deleted");
}
