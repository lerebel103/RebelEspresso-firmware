#include <freertos/FreeRTOS.h>

#include <esp_check.h>
#include <hal/gpio_types.h>
#include <hw_config.h>
#include <src/events.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_handle.hpp>
#include "brew.h"
#include "process_image.h"
#include "sys/nvram_store.h"

#define TAG "brew"
#define BREW_REFILL_NVS_STATUS_STORE "st.brew"
#define KEY_brew_count "brew_cnt"
#define KEY_descale_count "descale_cnt"
#define KEY_descale_last "descale_last"

// Minimum brew duration (seconds) to count as a real shot
#define MIN_BREW_DURATION_SEC 10

static brew_status_t s_status = {0, 0, 0};
static uint64_t s_brew_start_time = 0;

static void _load_nvram() {
  nvs_handle my_handle;
  ESP_ERROR_CHECK(nvs_open(BREW_REFILL_NVS_STATUS_STORE, NVS_READWRITE, &my_handle));

  nvs_get_u32(my_handle, KEY_brew_count, &s_status.brew_count);
  nvs_get_u32(my_handle, KEY_descale_count, &s_status.descale_count);
  nvs_get_i64(my_handle, KEY_descale_last, (int64_t *)&s_status.last_descale_time);

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

/**
 * Brew events from the I/O scan task.
 * The I/O scan handles switch debouncing and pump/relay actuation.
 * This handler only tracks brew statistics (count, duration) for NVS persistence.
 */
static void _brew_events([[maybe_unused]] void *handler_args, [[maybe_unused]] esp_event_base_t base, int32_t id,
                         [[maybe_unused]] void *event_data) {
  if (id == BREW_STARTED) {
    if (event_data != nullptr) {
      s_brew_start_time = *(uint64_t *)event_data;
    } else {
      s_brew_start_time = esp_timer_get_time();
    }
    ESP_LOGI(TAG, "Brew started (tracking duration)");
  } else if (id == BREW_STOPPED) {
    auto now_us = esp_timer_get_time();
    auto *img = process_image_get();

    // Count as a real brew shot if it lasted >10s and we're not descaling
    if (!img->descale_mode && s_brew_start_time != 0 &&
        (now_us - s_brew_start_time) > (uint64_t)(MIN_BREW_DURATION_SEC * 1e6)) {
      s_status.brew_count++;
      _save_nvram();
      ESP_LOGI(TAG, "Brew counted (#%lu)", (unsigned long)s_status.brew_count);
    }
    s_brew_start_time = 0;
  }
}

/**
 * Power events — track descale count.
 */
static void _power_events([[maybe_unused]] void *handler_args, [[maybe_unused]] esp_event_base_t base, int32_t id,
                          [[maybe_unused]] void *event_data) {
  if (id == POWER_ACTIVE) {
    auto *img = process_image_get();
    if (img->descale_mode) {
      s_status.descale_count++;
      s_status.last_descale_time = time(nullptr);
      _save_nvram();
      ESP_LOGI(TAG, "Descale mode activated (#%lu)", (unsigned long)s_status.descale_count);
    }
  }
}

brew_status_t brew_get_status() {
  return s_status;
}

void brew_init() {
  _load_nvram();

  // Register event handlers for brew statistics tracking
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, BREW_STARTED, _brew_events, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, BREW_STOPPED, _brew_events, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, POWER_ACTIVE, _power_events, nullptr));

  ESP_LOGI(TAG, "Initialised (brew count: %lu)", (unsigned long)s_status.brew_count);
}
