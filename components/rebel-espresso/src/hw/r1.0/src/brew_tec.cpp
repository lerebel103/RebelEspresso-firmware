
#include "brew_tec.h"

#include "rtds.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <hal/ledc_types.h>
#include <driver/ledc.h>
#include <esp_event.h>
#include <Max31865.h>

#include "hw_config.h"
#include "events.h"

#define TAG "TEC"

const uint8_t BREW_TEC_ENABLED_DEFAULT = 0;
const double BREW_TEC_HYSTERESIS_DEFAULT = 1.0;
const double BREW_TEC_MAX_TEMP_DEFAULT = 130.0;


static ledc_channel_config_t s_pwm_channel;
static esp_event_loop_handle_t s_event_loop;
static int s_duty = 0;

static brew_tec_cfg_t s_cfg;

static uint64_t s_last_stats_save = 0;
static bool s_stats_changed = false;
static brew_tec_status_t s_stats;
static pid_struct_t s_pid;

// Keep track of TEC temps on both sides
static window_value_t s_hot_data;
static window_value_t s_cold_data;

static void _load_stats() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEC_STATS_STORE, NVS_READWRITE, &my_handle));

    uint32_t defaultVal = 0;
    nvram_store_get_u32(my_handle, KEY_BREW_TEC_STATS_OVER_TEMP, (uint32_t *) &s_stats.temp_over_limit_count,
                        (void *) &defaultVal);
    nvram_store_get_u32(my_handle, KEY_BREW_TEC_STATS_TEMP_ERROR, (uint32_t *) &s_stats.temp_read_error_count,
                        (void *) &defaultVal);
    nvram_store_get_u32(my_handle, KEY_BREW_TEC_STATS_TEMP_RANGE_ERROR, (uint32_t *) &s_stats.temp_out_of_range_count,
                        (void *) &defaultVal);

    nvs_close(my_handle);

    s_last_stats_save = 0;
    s_stats_changed = false;
}

static void _save_stats(uint64_t time) {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEC_STATS_STORE, NVS_READWRITE, &my_handle));

    nvram_store_set_u32(my_handle, KEY_BREW_TEC_STATS_OVER_TEMP, (uint32_t *) &s_stats.temp_over_limit_count);
    nvram_store_set_u32(my_handle, KEY_BREW_TEC_STATS_TEMP_ERROR, (uint32_t *) &s_stats.temp_read_error_count);
    nvram_store_set_u32(my_handle, KEY_BREW_TEC_STATS_TEMP_RANGE_ERROR, (uint32_t *) &s_stats.temp_out_of_range_count);

    nvs_close(my_handle);
    s_last_stats_save = time;
    s_stats_changed = false;
}

static void _load_nvram() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEC_CFG_STORE, NVS_READWRITE, &my_handle));

    pid_load_nvram(my_handle, s_cfg.pid);
    nvram_store_get_u8(my_handle, KEY_BREW_TEC_ENABLED, (uint8_t *) &s_cfg.enabled,
                       (void *) &BREW_TEC_ENABLED_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_BREW_TEC_HYSTERESIS, (uint64_t *) &s_cfg.hysteresis,
                        (void *) &BREW_TEC_HYSTERESIS_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_BREW_TEC_MAX_TEMP, (uint64_t *) &s_cfg.max_tec_temp,
                        (void *) &BREW_TEC_MAX_TEMP_DEFAULT);
    nvs_close(my_handle);
}

static void _save_nvram() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEC_CFG_STORE, NVS_READWRITE, &my_handle));

    pid_save_nvram(my_handle, s_cfg.pid);
    nvram_store_set_u8(my_handle, KEY_BREW_TEC_ENABLED, (uint8_t *) &s_cfg.enabled);
    nvram_store_set_u64(my_handle, KEY_BREW_TEC_HYSTERESIS, (uint64_t *) &s_cfg.hysteresis);
    nvram_store_set_u64(my_handle, KEY_BREW_TEC_MAX_TEMP, (uint64_t *) &s_cfg.max_tec_temp);

    nvs_close(my_handle);
}


/*
 * Apply new duty to HBRIDGE
 *
 * @param duty integral [0-100]
 */
extern "C" void brew_tec_set_duty(int duty) {
    if (duty > 100) {
        duty = 100;
    } else if (duty < -100) {
        duty = -100;
    }

    // Apply the correct polarity for duty
    if (duty > 0) {
        gpio_set_level(PIN_OUT_HBRIDGE_DIR, 1);
    } else {
        gpio_set_level(PIN_OUT_HBRIDGE_DIR, 0);
    }

    ledc_set_duty(s_pwm_channel.speed_mode, s_pwm_channel.channel, (uint32_t) (1024 * abs(duty) / 100.0f));
    ledc_update_duty(s_pwm_channel.speed_mode, s_pwm_channel.channel);
    s_duty = duty;
    ESP_LOGD(TAG, "Duty set to %d", duty);
}

int brew_tec_get_duty() {
    return s_duty;
}

void _power_off_tec() {
    brew_tec_set_duty(0);
    gpio_set_level(PIN_OUT_HBRIDGE_DIS, 1);

    // Invalidate TEC temp records, new ones will come in
    s_hot_data.fault = (uint8_t)Max31865Error::RefHigh;
    s_cold_data.fault = (uint8_t)Max31865Error::RefHigh;
}

void brew_tec_process(uint64_t time_us, const window_value_t &data) {
    if (!s_cfg.enabled) {
        _power_off_tec();
        return;
    } else if (!(xEventGroupGetBits(status_event_group) & POWER_ON_BIT)) {
        ESP_LOGW(TAG, "In standby, not running.");
        _power_off_tec();
        return;
    } else if (xEventGroupGetBits(status_event_group) & DESCALE_MODE_BIT) {
        ESP_LOGI(TAG, "Descaling, not running");
        _power_off_tec();
        return;
    } else if (!(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT)) {
        ESP_LOGW(TAG, "Boiler level low, not running");
        _power_off_tec();
        return;
    } else if (data.fault != (uint8_t)Max31865Error::NoError) {
        ESP_LOGE(TAG, "Brew sensor error %s", Max31865::errorToString((Max31865Error)data.fault));
        s_stats.temp_read_error_count++;
        s_stats_changed = true;
        _power_off_tec();
        return;
    } else if (s_hot_data.fault != (uint8_t)Max31865Error::NoError) {
        ESP_LOGE(TAG, "TEC hot side sensor error %s", Max31865::errorToString((Max31865Error)s_hot_data.fault));
        _power_off_tec();
        s_stats.tec_hot_side_error_count++;
        s_stats_changed = true;
        return;
    } else if (s_cold_data.fault != (uint8_t)Max31865Error::NoError) {
        ESP_LOGE(TAG, "TEC cold side sensor error %s", Max31865::errorToString((Max31865Error)s_cold_data.fault));
        _power_off_tec();
        s_stats.tec_cold_side_error_count++;
        s_stats_changed = true;
        return;
    } else if (data.temperature > 110 || data.temperature < 5) {
        ESP_LOGE(TAG, "Brew temperature out of range: %f", data.temperature);
        s_stats.temp_out_of_range_count++;
        s_stats_changed = true;
        _power_off_tec();
        return;
    }

    // Ok, if we have a delta of more than 60 degree, we are stuffed, can't run control, quit
    if (abs(s_hot_data.temperature - s_cold_data.temperature) > 55) {
        ESP_LOGE(TAG, "TEC max delta exceeded: %f", abs(s_hot_data.temperature - s_cold_data.temperature));
        brew_tec_set_duty(0);
        gpio_set_level(PIN_OUT_HBRIDGE_DIS, 1);
        s_stats.tec_temp_delta_error_count++;
        s_stats_changed = true;
        return;
    }

    if (s_hot_data.temperature > s_cfg.max_tec_temp) {
        ESP_LOGE(TAG, "TEC hot side exceeded: %f", s_hot_data.temperature);
        brew_tec_set_duty(0);
        gpio_set_level(PIN_OUT_HBRIDGE_DIS, 1);
        s_stats.tec_temp_hot_thres_error_count++;
        s_stats_changed = true;
        return;
    }

    if (s_cold_data.temperature > s_cfg.max_tec_temp) {
        ESP_LOGE(TAG, "TEC cold side exceeded: %f", s_cold_data.temperature);
        brew_tec_set_duty(0);
        gpio_set_level(PIN_OUT_HBRIDGE_DIS, 1);
        s_stats.tec_temp_cold_thres_error_count++;
        s_stats_changed = true;
        return;
    }

    // Check if we have h-bridge report and error, attempt to reset it
    if (gpio_get_level(PIN_IN_HBRIDGE_SO)) {
        gpio_set_level(PIN_OUT_HBRIDGE_DIS, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGW(TAG, "Got error from h-bridge, turning off and on");
        gpio_set_level(PIN_OUT_HBRIDGE_DIS, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        s_stats.tec_ic_error++;
    }

    // Run pid to get new duty
    auto result = pid_process(s_pid, s_cfg.pid, time_us, data);
    if (result.is_over_threshold) {
        ESP_LOGW(TAG, "Over temp threshold exceeded");
        s_stats.temp_over_limit_count++;
        s_stats_changed = true;
    }

    if (result.duty == 0) {
        gpio_set_level(PIN_OUT_HBRIDGE_DIS, 1);
    }
    brew_tec_set_duty(result.duty);
    ESP_LOGI(TAG, "Brew temp=%f, duty=%d, setpoint=%f",
             data.temperature, s_duty, s_cfg.pid.setpoints[s_cfg.pid.active_setpoint]);
}

void brew_tec_hot_updated(uint64_t time_us, const window_value_t &data) {
    s_hot_data = data;
}

void brew_tec_cold_updated(uint64_t time_us, const window_value_t &data) {
    s_cold_data = data;
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        ESP_LOGI(TAG, "Powering down TEC");
        brew_tec_set_duty(0);
        _power_off_tec();
    } else if (id == POWER_ACTIVE) {
        if (s_cfg.enabled) {
            ESP_LOGI(TAG, "Resuming TEC");
            gpio_set_level(PIN_OUT_HBRIDGE_DIS, 0);
            pid_reset(s_pid);
        }
    }
}

static void _brew_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == BREW_STARTED) {
        ESP_LOGI(TAG, "Brew started");
    } else if (id == BREW_STOPPED) {
        ESP_LOGI(TAG, "Brew stopped");
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

static void _init_h_bridge() {
    ledc_timer_config_t ledc_timer;
    ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;          // timer mode
    ledc_timer.clk_cfg = LEDC_USE_APB_CLK;
    ledc_timer.duty_resolution = LEDC_TIMER_10_BIT; // resolution of PWM duty
    ledc_timer.timer_num = LEDC_TIMER_0;            // timer index
    ledc_timer.freq_hz = 20000;                      // frequency of PWM signal
    // Set configuration of timer0 for high speed channels
    ledc_timer_config(&ledc_timer);

    s_pwm_channel.channel = LEDC_CHANNEL_0;
    s_pwm_channel.duty = 0;
    s_pwm_channel.gpio_num = PIN_OUT_HBRIDGE_PWM;
    s_pwm_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    s_pwm_channel.hpoint = 0;
    s_pwm_channel.timer_sel = LEDC_TIMER_0;
    ledc_channel_config(&s_pwm_channel);

    ESP_LOGI(TAG, "TEC initialised.");
}


void brew_tec_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
    _load_nvram();
    _load_stats();
    pid_init(s_pid);

    // Configure pins for H-Bridge

    // Output pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << PIN_OUT_HBRIDGE_DIR) |
            (1ULL << PIN_OUT_HBRIDGE_DIS) |
            (1ULL << PIN_OUT_HBRIDGE_PWM)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Input pins
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << PIN_IN_HBRIDGE_SO)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    _init_h_bridge();

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


    // Enable H-Bridge
    gpio_set_level(PIN_OUT_HBRIDGE_DIS, 1);
}

void brew_tec_delete() {
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, BREW_STARTED, _brew_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, BREW_STOPPED, _brew_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, TICK, _tick_events));
    pid_init(s_pid);
}


const brew_tec_cfg_t &brew_tec_get_cfg() {
    return s_cfg;
}

void brew_tec_set_cfg(brew_tec_cfg_t config) {
    // validate all fields
    pid_update(s_cfg.pid, config.pid);

    s_cfg.enabled = config.enabled;

    if (config.hysteresis > 0 && config.hysteresis < 10) {
        s_cfg.hysteresis = config.hysteresis;
    }

    if (config.max_tec_temp > 0 && config.max_tec_temp < 180) {
        s_cfg.max_tec_temp = config.max_tec_temp;
    }

    // Save what we can then
    _save_nvram();
}

void brew_tec_update_cfg(const cJSON *json) {
    brew_tec_cfg_t new_config = s_cfg;
    new_config.from_json(json);
    brew_tec_set_cfg(new_config);
}


void brew_tec_reset_cfg() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEC_CFG_STORE, NVS_READWRITE, &my_handle));
    nvs_erase_all(my_handle);
    nvs_close(my_handle);

    _load_nvram();
}

const brew_tec_status_t &brew_tec_get_status() {
    return s_stats;
}

void brew_tec_reset_stats() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEC_STATS_STORE, NVS_READWRITE, &my_handle));
    nvs_erase_all(my_handle);
    nvs_close(my_handle);

    _load_stats();
}

double brew_tec_setpoint_inc(double inc) {
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
        ESP_ERROR_CHECK(nvs_open(NVS_BREW_TEC_CFG_STORE, NVS_READWRITE, &my_handle));
        pid_save_setpoint(my_handle, s_cfg.pid);
        nvs_close(my_handle);
    }

    return s_cfg.pid.setpoints[s_cfg.pid.active_setpoint];
}

void brew_tec_set_active_setpoint(int idx) {
    if (idx >= 0 && idx < MAX_SETPOINTS) {
        s_cfg.pid.active_setpoint = idx;
    }
}

