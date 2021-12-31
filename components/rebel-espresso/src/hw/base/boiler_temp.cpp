#include <esp_log.h>
#include <driver/rmt.h>
#include <cmath>
#include <src/events.h>
#include <esp_event.h>
#include <src/sys/nvram_store.h>

#include "rtds.h"
#include "boiler_temp.h"
#include "rmt_duty_map.h"
#include "brew_temp.h"

#define TAG "Boiler"

#define RMT_CLK_DIV 160
#define RMT_TX_CHANNEL RMT_CHANNEL_0

const uint8_t BOILER_MAINS_HZ_DEFAULT = 50;
const uint16_t BOILER_TEMP_ERROR_RESTART_SEC_DEFAULT = 60;
const uint16_t BOILER_FULL_DUTY_PID_ERROR_THRESHOLD_DEFAULT = 10;

static esp_event_loop_handle_t s_event_loop;
static boiler_temp_cfg_t s_cfg;

static uint64_t s_last_stats_save = 0;
static bool s_stats_changed = false;
static boiler_temp_status_t s_stats = {};
static int s_last_duty = 0;
static double s_acc_duty = 0;
static double s_boiler_error_sec = 0;
static pid_struct_t s_pid;
double s_trimmed_setpoint = 0;


static void _load_stats() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BOILER_STATS_STORE, NVS_READWRITE, &my_handle));

    uint32_t defaultVal = 0;
    nvram_store_get_u32(my_handle, KEY_BOILER_STATS_OVER_TEMP, (uint32_t *) &s_stats.temp_over_limit_count,
                        (void *) &defaultVal);
    nvram_store_get_u32(my_handle, KEY_BOILER_STATS_TEMP_ERROR, (uint32_t *) &s_stats.temp_read_error_count,
                        (void *) &defaultVal);
    nvram_store_get_u32(my_handle, KEY_BOILER_STATS_TEMP_RANGE_ERROR, (uint32_t *) &s_stats.temp_out_of_range_count,
                        (void *) &defaultVal);

    nvs_close(my_handle);

    s_last_stats_save = 0;
    s_stats_changed = false;
}

static void _save_stats(uint64_t time) {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BOILER_STATS_STORE, NVS_READWRITE, &my_handle));

    nvram_store_set_u32(my_handle, KEY_BOILER_STATS_OVER_TEMP, (uint32_t *) &s_stats.temp_over_limit_count);
    nvram_store_set_u32(my_handle, KEY_BOILER_STATS_TEMP_ERROR, (uint32_t *) &s_stats.temp_read_error_count);
    nvram_store_set_u32(my_handle, KEY_BOILER_STATS_TEMP_RANGE_ERROR, (uint32_t *) &s_stats.temp_out_of_range_count);

    nvs_close(my_handle);
    s_last_stats_save = time;
    s_stats_changed = false;
}

static void _load_nvram() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BOILER_CFG_STORE, NVS_READWRITE, &my_handle));

    pid_load_nvram(my_handle, s_cfg.pid);
    nvram_store_get_u8(my_handle, KEY_BOILER_MAINS_HZ, (uint8_t *) &s_cfg.mains_hz,
                       (void *) &BOILER_MAINS_HZ_DEFAULT);
    nvram_store_get_u16(my_handle, KEY_BOILER_TEMP_ERROR_RESTART_SEC, &s_cfg.temp_error_restart_time_sec,
                        (void *) &BOILER_TEMP_ERROR_RESTART_SEC_DEFAULT);
    nvram_store_get_u8(my_handle, KEY_BOILER_FULL_DUTY_ERROR_THRESHOLD, &s_cfg.full_duty_pid_error_threshold,
                       (void *) &BOILER_FULL_DUTY_PID_ERROR_THRESHOLD_DEFAULT);

    nvs_close(my_handle);
}

static void _save_nvram() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BOILER_CFG_STORE, NVS_READWRITE, &my_handle));

    pid_save_nvram(my_handle, s_cfg.pid);
    nvram_store_set_u8(my_handle, KEY_BOILER_MAINS_HZ, (uint8_t *) &s_cfg.mains_hz);
    nvram_store_set_u16(my_handle, KEY_BOILER_TEMP_ERROR_RESTART_SEC, &s_cfg.temp_error_restart_time_sec);
    nvram_store_set_u8(my_handle, KEY_BOILER_FULL_DUTY_ERROR_THRESHOLD, &s_cfg.full_duty_pid_error_threshold);

    nvs_close(my_handle);
}

/*
 * Apply new duty to SSR
 *
 * @param duty integral [0-100]
 */
extern "C" void boiler_temp_set_duty(int duty) {
    if (duty > 100) {
        duty = 100;
    } else if (duty < 0) {
        duty = 0;
    }

    const struct rmt_pulse_t *pulses = rmt_duty_get_pulses(duty, s_cfg.mains_hz);
    ESP_ERROR_CHECK(rmt_fill_tx_items(RMT_TX_CHANNEL, pulses->items, pulses->num_items, false));
    s_last_duty = duty;
}

int boiler_temp_get_duty() {
    return s_last_duty;
}

/*
 * Turns power off immediately to ssr
 */
static void _power_off_ssr() {
    // Turn off RMT and force pin to zero as safety
    boiler_temp_set_duty(0);
    rmt_tx_stop(RMT_TX_CHANNEL);
    gpio_set_level(BOILER_SSR_PIN, 0);
}

/*
 * Initialize the RMT Tx channel
 */
static void _rmt_tx_init() {
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(BOILER_SSR_PIN, RMT_TX_CHANNEL);

    // Disable carrier and enable loop back so we can generate pulses
    config.tx_config.carrier_en = false;
    config.tx_config.loop_en = false;
    config.tx_config.idle_output_en = true;

    // set the maximum clock divider to be able to output
    // RMT pulses in range of about one hundred milliseconds
    config.clk_div = RMT_CLK_DIV;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // Set zero duty and enable loop so we continuously tx the last duty pulses
    boiler_temp_set_duty(0);
    rmt_set_tx_loop_mode(config.channel, true);
}


static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        ESP_LOGI(TAG, "Powering down Boiler SSR");
        _power_off_ssr();
    } else if (id == POWER_ACTIVE) {
        ESP_LOGI(TAG, "Resuming Boiler SSR");
        pid_reset(s_pid);
    }
}

static void _tick_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
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

bool is_primary_setpoint() { return s_cfg.pid.active_setpoint == 0; }

double boiler_temp_get_current_setpoint() {
    if (is_primary_setpoint()) {
        return s_trimmed_setpoint;
    } else {
        return s_cfg.pid.setpoints[s_cfg.pid.active_setpoint];
    }
}

void boiler_temp_process(uint64_t time_us, const reading_t &data) {
    if (!(xEventGroupGetBits(status_event_group) & POWER_ON_BIT)) {
        ESP_LOGD(TAG, "In standby, not running.");
        _power_off_ssr();
        return;
    } else if (xEventGroupGetBits(status_event_group) & DESCALE_MODE_BIT) {
        ESP_LOGI(TAG, "Descaling, not running");
        _power_off_ssr();
        return;
    } else if (!(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT)) {
        ESP_LOGW(TAG, "Boiler level low, not running");
        _power_off_ssr();
        return;
    } else if (data.fault != (uint8_t) RTD_NoError) {
        ESP_LOGE(TAG, "Boiler sensor error: %d", data.fault);
        s_stats.temp_read_error_count++;
        s_stats_changed = true;
        _power_off_ssr();

        // If we get successive errors from the boiler restart
        if (s_pid.last_time_us != 0) {
            s_boiler_error_sec += (time_us - s_pid.last_time_us) * 1e-6;
        }
        if (s_cfg.temp_error_restart_time_sec != 0 && s_boiler_error_sec > s_cfg.temp_error_restart_time_sec) {
            ESP_LOGE(TAG, "Restarting, too many RTD errors received in succession.");
            esp_restart();
        }

        s_pid.last_time_us = time_us;
        return;
    } else if (data.value > 150 || data.value < 5) {
        ESP_LOGE(TAG, "Boiler temperature out of range: %f", data.value);
        s_stats.temp_out_of_range_count++;
        s_stats_changed = true;
        _power_off_ssr();
        return;
    }

    // No errors from RTD, all good reset.
    s_boiler_error_sec = 0;

    // Add trim as needed to target setpoint to maintain brew temp, but only for index == 0
    auto setpoint = s_cfg.pid.setpoints[s_cfg.pid.active_setpoint];
    auto trim = brew_temp_get_trim();
    if (is_primary_setpoint()) {
        // Safety guard
        if (s_trimmed_setpoint == 0) {
            s_trimmed_setpoint = setpoint;
        }

        if (trim.active) {
            // Apply trim
            s_trimmed_setpoint += trim.value;
        }

        // Cap trim always
        if (s_trimmed_setpoint > s_cfg.pid.setpoints[s_cfg.pid.active_setpoint]) {
            s_trimmed_setpoint = s_cfg.pid.setpoints[s_cfg.pid.active_setpoint];
        } else if (s_trimmed_setpoint < 102) {
            s_trimmed_setpoint = 102;
        }

        setpoint = s_trimmed_setpoint;
    }


    // Make a copy of config to dampen setpoint
    auto pid_cfg = s_cfg.pid;
    pid_cfg.setpoints[pid_cfg.active_setpoint] = setpoint;
    ESP_LOGI(TAG, "Boiler setpoint damping: %fC", setpoint - s_cfg.pid.setpoints[s_cfg.pid.active_setpoint]);

    // Run pid to get new duty
    auto result = pid_process(s_pid, pid_cfg, time_us, data);

    if (result.is_over_threshold) {
        ESP_LOGW(TAG, "Over temp threshold exceeded");
        s_stats.temp_over_limit_count++;
        s_stats_changed = true;

        // Then stop
        s_acc_duty = 0;
    } else if (s_pid.last_pid_err > s_cfg.full_duty_pid_error_threshold) {
        // Then we are not wanting PID, apply 100% duty
        s_acc_duty = 100;
    } else if (s_pid.last_pid_err > 1 && s_pid.last_derivative >= 1) {
        // In this case we've had a very large drop in temperature, apply 100%
        // this happens when the steam tap is opened or water refill kicks in
        s_acc_duty = 100;
    } else if (s_pid.last_pid_err <= 4 && s_pid.last_derivative <= -0.4) {
        // Then we've just come out of a disturbance and are recovering fast.
        // This happens when steam tap is closed. Let things settle again naturally
        s_acc_duty = 0;
    } else {
        s_acc_duty += result.duty;
    }

    if (s_acc_duty < 0) {
        s_acc_duty = 0;
    } else if (s_acc_duty > 100) {
        s_acc_duty = 100;
    }

    if (s_acc_duty == 0) {
        _power_off_ssr();
    } else {
        boiler_temp_set_duty((int)(round(s_acc_duty)));
    }
    ESP_LOGW(TAG, "Boiler temp=%f, pid_duty=%f, duty=%f, setpoint=%f",
             data.value, result.duty, s_acc_duty, setpoint);
}


void boiler_temp_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;

    _load_nvram();
    _load_stats();

    _rmt_tx_init();
    pid_init(s_pid);
    s_trimmed_setpoint = s_cfg.pid.setpoints[s_cfg.pid.active_setpoint];

    // Get our power events in place so we can run the process loop as needed
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, TICK,
                                                    _tick_events, s_event_loop));
}

void boiler_temp_delete() {
    rmt_driver_uninstall(RMT_TX_CHANNEL);
    pid_reset(s_pid);

    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, TICK, _tick_events));
}

const struct boiler_temp_cfg_t &boiler_temp_get_cfg() {
    return s_cfg;
}

void boiler_temp_set_cfg(boiler_temp_cfg_t config) {
    // validate all fields
    pid_update(s_cfg.pid, config.pid);

    if (config.mains_hz == 50) {
        s_cfg.mains_hz = MAINS_50HZ;
    } else if (config.mains_hz == 60) {
        s_cfg.mains_hz = MAINS_60HZ;
    }
    s_cfg.temp_error_restart_time_sec = config.temp_error_restart_time_sec;
    s_cfg.full_duty_pid_error_threshold = config.full_duty_pid_error_threshold;

    // Save what we can then
    _save_nvram();
}

void boiler_temp_update_cfg(const cJSON *json) {
    boiler_temp_cfg_t new_config = s_cfg;
    new_config.from_json(json);
    boiler_temp_set_cfg(new_config);
}

void boiler_temp_reset_cfg() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BOILER_CFG_STORE, NVS_READWRITE, &my_handle));
    nvs_erase_all(my_handle);
    nvs_close(my_handle);

    _load_nvram();
}

const boiler_temp_status_t &boiler_temp_get_status() {
    return s_stats;
}

void boiler_temp_reset_stats() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BOILER_STATS_STORE, NVS_READWRITE, &my_handle));
    nvs_erase_all(my_handle);
    nvs_close(my_handle);

    _load_stats();
}

double boiler_setpoint_inc(double inc) {
    auto new_val = s_cfg.pid.setpoints[s_cfg.pid.active_setpoint] + inc;
    if (s_cfg.pid.active_setpoint == 0) {
        if (new_val < SETPOINT0_MIN) {
            new_val = SETPOINT0_MIN;
        }
        if (new_val > SETPOINT0_MAX) {
            new_val = SETPOINT0_MAX;
        }
    } else {
        if (new_val < SETPOINT1_MIN) {
            new_val = SETPOINT1_MIN;
        }
        if (new_val > SETPOINT1_MAX) {
            new_val = SETPOINT1_MAX;
        }
    }

    if (new_val != s_cfg.pid.setpoints[s_cfg.pid.active_setpoint]) {
        s_cfg.pid.setpoints[s_cfg.pid.active_setpoint] = new_val;

        nvs_handle my_handle;
        ESP_ERROR_CHECK(nvs_open(NVS_BOILER_CFG_STORE, NVS_READWRITE, &my_handle));
        pid_save_setpoint(my_handle, s_cfg.pid);
        nvs_close(my_handle);
    }

    return s_cfg.pid.setpoints[s_cfg.pid.active_setpoint];
}

void boiler_set_active_setpoint(int idx) {
    if (idx >= 0 && idx < MAX_SETPOINTS) {
        s_cfg.pid.active_setpoint = idx;
    }
}


