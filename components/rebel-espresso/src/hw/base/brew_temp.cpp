#include "brew_temp.h"

#include <esp_log.h>
#include <freertos/task.h>
#include <src/events.h>
#include <esp_event.h>
#include <hw_config.h>
#include <Max31865.h>

#include "rtds.h"
#include "pid.h"

#define TAG "BrewTemp"

const uint8_t BREW_TEMP_ENABLED_DEFAULT = 1;
const double BREW_TEMP_PERC_DEFAULT = 10.0;
const double BREW_TEMP_RESET_SEC_DEFAULT = 3 * 60;

static esp_event_loop_handle_t s_event_loop;
static brew_temp_trim_t s_trim = {};
static uint64_t s_last_brew_time = 0;

static brew_temp_cfg_t s_cfg;

static uint64_t s_last_stats_save = 0;
static bool s_stats_changed = false;
static brew_temp_status_t s_stats;
static pid_struct_t s_pid;

extern "C" void brew_temp_fake_trim_active() {
    s_trim.active = true;
}

static void _load_stats() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEMP_STATS_STORE, NVS_READWRITE, &my_handle));

    uint32_t defaultVal = 0;
    nvram_store_get_u32(my_handle, KEY_BREW_TEMP_STATS_OVER_TEMP, (uint32_t *) &s_stats.brew_temp_over_limit_count,
                        (void *) &defaultVal);
    nvram_store_get_u32(my_handle, KEY_BREW_TEMP_STATS_TEMP_ERROR, (uint32_t *) &s_stats.brew_temp_read_error_count,
                        (void *) &defaultVal);
    nvram_store_get_u32(my_handle, KEY_BREW_TEMP_STATS_TEMP_RANGE_ERROR,
                        (uint32_t *) &s_stats.brew_temp_out_of_range_count,
                        (void *) &defaultVal);

    nvs_close(my_handle);

    s_last_stats_save = 0;
    s_stats_changed = false;
}

static void _save_stats(uint64_t time) {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEMP_STATS_STORE, NVS_READWRITE, &my_handle));

    nvram_store_set_u32(my_handle, KEY_BREW_TEMP_STATS_OVER_TEMP, (uint32_t *) &s_stats.brew_temp_over_limit_count);
    nvram_store_set_u32(my_handle, KEY_BREW_TEMP_STATS_TEMP_ERROR, (uint32_t *) &s_stats.brew_temp_read_error_count);
    nvram_store_set_u32(my_handle, KEY_BREW_TEMP_STATS_TEMP_RANGE_ERROR,
                        (uint32_t *) &s_stats.brew_temp_out_of_range_count);

    nvs_close(my_handle);
    s_last_stats_save = time;
    s_stats_changed = false;
}

static void _load_nvram() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEMP_CFG_STORE, NVS_READWRITE, &my_handle));

    pid_load_nvram(my_handle, s_cfg.pid);

    nvram_store_get_u8(my_handle, KEY_BREW_TEMP_ENABLED, (uint8_t *) &s_cfg.enabled,
                       (void *) &BREW_TEMP_ENABLED_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_BREW_TEMP_PERC, (uint64_t *) &s_cfg.max_damping_perc,
                        (void *) &BREW_TEMP_PERC_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_BREW_TEMP_RESET_SEC, (uint64_t *) &s_cfg.reset_time_sec,
                        (void *) &BREW_TEMP_RESET_SEC_DEFAULT);
    nvs_close(my_handle);
}

static void _save_nvram() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEMP_CFG_STORE, NVS_READWRITE, &my_handle));

    pid_save_nvram(my_handle, s_cfg.pid);
    nvram_store_set_u8(my_handle, KEY_BREW_TEMP_ENABLED, (uint8_t *) &s_cfg.enabled);
    nvram_store_set_u64(my_handle, KEY_BREW_TEMP_PERC, (uint64_t *) &s_cfg.max_damping_perc);
    nvram_store_set_u64(my_handle, KEY_BREW_TEMP_RESET_SEC, (uint64_t *) &s_cfg.reset_time_sec);

    nvs_close(my_handle);
}

brew_temp_trim_t brew_temp_get_trim() {
    if (!s_cfg.enabled) {
        s_trim.active = false;
    }
    return s_trim;
}

double brew_temp_get_setpoint() {
    return s_cfg.pid.setpoints[s_cfg.pid.active_setpoint];
}

void brew_temp_set_setpoint(double setpoint) {
    s_cfg.pid.setpoints[s_cfg.pid.active_setpoint] = setpoint;
    brew_temp_set_cfg(s_cfg);
    xEventGroupSetBits(status_event_group, SEND_STATE_BIT);
}

void brew_temp_process(uint64_t time_us, const window_value_t &brew_head_data) {
    if (!s_cfg.enabled) {
        s_trim.active = false;
        return;
    } else if (!(xEventGroupGetBits(status_event_group) & POWER_ON_BIT)) {
        ESP_LOGW(TAG, "In standby, not running.");
        s_trim.active = false;
        return;
    } else if (xEventGroupGetBits(status_event_group) & DESCALE_MODE_BIT) {
        ESP_LOGI(TAG, "Descaling, not running");
        s_trim.active = false;
        return;
    } else if (!(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT)) {
        ESP_LOGW(TAG, "Boiler level low, not running");
        s_trim.active = false;
        return;
    } else if (brew_head_data.fault != (uint8_t)Max31865Error::NoError) {
        ESP_LOGE(TAG, "Boiler sensor error %s", Max31865::errorToString((Max31865Error)brew_head_data.fault));
        s_stats.brew_temp_read_error_count++;
        s_stats_changed = true;
        s_trim.active = false;
        return;
    } else if (brew_head_data.temperature > 150 || brew_head_data.temperature < 5) {
        ESP_LOGE(TAG, "Boiler temperature out of range: %f", brew_head_data.temperature);
        s_stats.brew_temp_out_of_range_count++;
        s_stats_changed = true;
        s_trim.active = false;
        return;
    } else if (s_last_brew_time != 0 && time_us - s_last_brew_time < s_cfg.reset_time_sec * 1e6) {
        // Then brew just happened, don't worry about it
        s_trim.active = false;
        return;
    }

    // Run pid to get new duty, passthrough upstream setpoint, always
    auto result = pid_process(s_pid, s_cfg.pid, time_us, brew_head_data);

    if (result.is_over_threshold) {
        ESP_LOGW(TAG, "Over temp threshold exceeded");
        s_stats.brew_temp_over_limit_count++;
        s_stats_changed = true;
        s_trim.active = false;
    } else {
        s_trim.active = true;
        s_trim.value = result.duty;

        static window_data_t wdata = {};
        window_data(&s_pid.data_window, &wdata);

        // 1 deg, 1 minutes
        // 60 ticks of 1 sec
        // P = 1 / 60 = 0.016
        // I = P / 10 = 0.0016
        // D = 60 = 60

        ESP_LOGD(TAG, "************************* brew temp: %f, Trim: %f, P=%f, I=%f, D=%f",
                brew_head_data.temperature, s_trim.value,
                 s_cfg.pid.P * (brew_temp_get_setpoint() - brew_head_data.temperature),
                 s_cfg.pid.I * wdata.error_integral, s_cfg.pid.D * wdata.derivative);
    }
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        s_trim.active = false;
    } else if (id == POWER_ACTIVE) {
        pid_reset(s_pid);
        s_last_brew_time = 0;
    }
}

static void _brew_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == BREW_STARTED) {
        ESP_LOGI(TAG, "Brew started, resetting damper period");
        uint64_t now = 0;
        if (event_data != nullptr) {
            now = *(uint64_t *) event_data;
        }

        s_last_brew_time = now;
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

void brew_temp_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
    _load_nvram();
    _load_stats();
    s_trim = { .active = false, .value = 0 };
    s_last_brew_time = 0;
    pid_init(s_pid);

    // Get our power events in place so we can run the process loop as needed
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BREW_STOPPED,
                                                    _brew_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BREW_STARTED,
                                                    _brew_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, TICK,
                                                    _tick_events, s_event_loop));
}

void brew_temp_delete() {
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, BREW_STARTED, _brew_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, BREW_STOPPED, _brew_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, TICK, _tick_events));
    pid_init(s_pid);
}

// for testing purposes only
extern "C" void brew_temp_set_offset(brew_temp_trim_t offset) {
    s_trim = offset;
}

const brew_temp_cfg_t &brew_temp_get_cfg() {
    return s_cfg;
}

void  brew_temp_set_cfg(brew_temp_cfg_t config) {
    // validate all fields
    pid_update(s_cfg.pid, config.pid);

    s_cfg.enabled = config.enabled;

    if (config.max_damping_perc > 0 && config.max_damping_perc < 100) {
        s_cfg.max_damping_perc = config.max_damping_perc;
    }

    if (config.reset_time_sec > 0 && config.reset_time_sec < 10 * 60) {
        s_cfg.reset_time_sec = config.reset_time_sec;
    }

    // Save what we can then
    _save_nvram();
}

void brew_temp_update_cfg(const cJSON *json) {
    brew_temp_cfg_t new_config = s_cfg;
    new_config.from_json(json);
    brew_temp_set_cfg(new_config);
}


void brew_temp_reset_cfg() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEMP_CFG_STORE, NVS_READWRITE, &my_handle));
    nvs_erase_all(my_handle);
    nvs_close(my_handle);

    _load_nvram();
}

const brew_temp_status_t &brew_temp_get_status() {
    return s_stats;
}

void brew_temp_reset_stats() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEMP_STATS_STORE, NVS_READWRITE, &my_handle));
    nvs_erase_all(my_handle);
    nvs_close(my_handle);

    _load_stats();
}

