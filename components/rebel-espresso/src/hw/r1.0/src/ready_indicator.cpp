#include "ready_indicator.h"

#include "rtds.h"
#include <esp_log.h>
#include <freertos/task.h>
#include <src/events.h>
#include <esp_event.h>
#include <hw_config.h>
#include <src/hw/base/out_signals.h>
#include "pid.h"
#include "brew_temp.h"

#define TAG "BrewTempDamper"

const uint8_t READY_INDICATOR_ENABLED_DEFAULT = 1;
const double READY_INDICATOR_DELTA_DEFAULT = 0.8;
const double READY_INDICATOR_HYSTERESIS_DEFAULT = 2.0;

static esp_event_loop_handle_t s_event_loop;
static ready_indicator_cfg_t s_cfg;

static uint64_t s_last_stats_save = 0;
static bool s_stats_changed = false;
static ready_indicator_status_t s_stats;

static void _load_stats() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_READY_INDICATOR_STATS_STORE, NVS_READWRITE, &my_handle));


    nvs_close(my_handle);

    s_last_stats_save = 0;
    s_stats_changed = false;
}

static void _save_stats(uint64_t time) {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_READY_INDICATOR_STATS_STORE, NVS_READWRITE, &my_handle));

    nvs_close(my_handle);
    s_last_stats_save = time;
    s_stats_changed = false;
}

static void _load_nvram() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_READY_INDICATOR_CFG_STORE, NVS_READWRITE, &my_handle));

    nvram_store_get_u8(my_handle, KEY_READY_INDICATOR_ENABLED, (uint8_t *) &s_cfg.enabled,
                       (void *) &READY_INDICATOR_ENABLED_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_READY_INDICATOR_DELTA, (uint64_t *) &s_cfg.delta,
                        (void *) &READY_INDICATOR_DELTA_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_READY_INDICATOR_HYSTERESIS, (uint64_t *) &s_cfg.hysteresis,
                        (void *) &READY_INDICATOR_HYSTERESIS_DEFAULT);
    nvs_close(my_handle);
}

static void _save_nvram() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_READY_INDICATOR_CFG_STORE, NVS_READWRITE, &my_handle));

    nvram_store_set_u8(my_handle, KEY_READY_INDICATOR_ENABLED, (uint8_t *) &s_cfg.enabled);
    nvram_store_set_u64(my_handle, KEY_READY_INDICATOR_DELTA, (uint64_t *) &s_cfg.delta);
    nvram_store_set_u64(my_handle, KEY_READY_INDICATOR_HYSTERESIS, (uint64_t *) &s_cfg.hysteresis);

    nvs_close(my_handle);
}



void ready_indicator_process(uint64_t time_us, const reading_t &brew_head_data) {
    if (!s_cfg.enabled ||
        !(xEventGroupGetBits(status_event_group) & POWER_ON_BIT)) {
        out_signals_set_level(OUT_SIGNALS_RELAY3, 0);
        return;
    }

    // Work out if we can light up the ready light, within range
    auto setpoint = brew_temp_get_setpoint();
    if (brew_head_data.value + s_cfg.delta >= setpoint) {
        out_signals_set_level(OUT_SIGNALS_RELAY3, 1);
    } else if ((brew_head_data.value + s_cfg.hysteresis) < setpoint) {
        out_signals_set_level(OUT_SIGNALS_RELAY3, 0);
    }
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        // Always off
        out_signals_set_level(OUT_SIGNALS_RELAY3, 0);
    }
}

static void _tick_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (!s_cfg.enabled) {
        return;
    }
    if (id != TICK) {
        return;
    }

    uint64_t now = 0;
    if (event_data != nullptr) {
        now = *(uint64_t *) event_data;
    }

    // See if we need to serialise stats, but pace it so we don't kill the flash
    if (s_stats_changed && (s_last_stats_save == 0 || (now - s_last_stats_save) >= (uint64_t) 5e6)) {
        _save_stats(now);
    }
}

void ready_indicator_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
    _load_nvram();
    _load_stats();

    out_signals_set_level(OUT_SIGNALS_RELAY3, 0);

    // Get our power events in place so we can run the process loop as needed
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, TICK,
                                                    _tick_events, s_event_loop));
}

void ready_indicator_delete() {
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, TICK, _tick_events));
}

const ready_indicator_cfg_t &ready_indicator_get_cfg() {
    return s_cfg;
}

void ready_indicator_set_cfg(ready_indicator_cfg_t config) {
    // validate all fields
    s_cfg.enabled = config.enabled;

    if (config.delta > 0 && config.delta < 10) {
        s_cfg.delta = config.delta;
    }

    if (config.hysteresis > 0 && config.hysteresis < 10) {
        s_cfg.hysteresis = config.hysteresis;
    }

    // Save what we can then
    _save_nvram();
}

void ready_indicator_update_cfg(const cJSON *json) {
    ready_indicator_cfg_t new_config = s_cfg;
    new_config.from_json(json);
    ready_indicator_set_cfg(new_config);
}


void ready_indicator_reset_cfg() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_READY_INDICATOR_CFG_STORE, NVS_READWRITE, &my_handle));
    nvs_erase_all(my_handle);
    nvs_close(my_handle);

    _load_nvram();
}

const ready_indicator_status_t &ready_indicator_get_status() {
    return s_stats;
}

void ready_indicator_reset_stats() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_READY_INDICATOR_STATS_STORE, NVS_READWRITE, &my_handle));
    nvs_erase_all(my_handle);
    nvs_close(my_handle);

    _load_stats();
}

