#include <hal/gpio_types.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <hw_config.h>
#include <esp_log.h>
#include "boiler_refill.h"
#include <esp_adc_cal.h>
#include <src/events.h>
#include <esp_event.h>
#include <src/sys/nvram_store.h>

#define DEFAULT_VREF                1100

const static char* TAG = "refill";

#define BOILER_REFILL_NVS_CFG_STORE     "cfg.b_refill"

const uint16_t BOILER_REFILL_START_DELAY_MS_DEFAULT = 1000;
const uint16_t BOILER_REFILL_STABILISE_MS_DEFAULT = 10;
const uint16_t BOILER_REFILL_ADC_NUM_READINGS_DEFAULT = 32;
const uint16_t BOILER_REFILL_REFILL_VOLTAGE_THRESHOLD_DEFAULT = 1500;
const uint16_t BOILER_REFILL_MAX_REFILL_TIME_MS_DEFAULT = 8000;
const uint16_t BOILER_REFILL_LEVEL_LOW_HYSTERESIS_MS_DEFAULT = 750;
const uint16_t BOILER_REFILL_LEVEL_OK_HYSTERESIS_MS_DEFAULT = 1000;

static boiler_refill_cfg_t s_cfg;
static boiler_refill_status_t s_status;

static const adc_unit_t unit = ADC_UNIT_1;
static esp_adc_cal_characteristics_t *adc_chars;
static esp_event_loop_handle_t s_event_loop;

static TickType_t s_refill_start_ms = 0;
static TickType_t s_begin_low_ms = 0;
static TickType_t s_begin_ok_ms = 0;

typedef void(*state_fn)(bool level_ok, TickType_t now_ms);

static state_fn current_state_fn;
static check_level_fn check_level;

static void _state_unknown(bool level_ok, TickType_t now_ms);
static void _state_not_filling(bool level_ok, TickType_t now_ms);
static void _state_filling(bool level_ok, TickType_t now_ms);
static void _state_error(bool level_ok, TickType_t now_ms);
static void _load_nvram();

    bool boiler_check_level() {
    // Enable voltage on probe
    gpio_set_level(PIN_WATER_LEVEL_ENABLE, 1);
    vTaskDelay(pdMS_TO_TICKS(s_cfg.stabilise_ms));

    double level_voltage = 0;
    for (int i=0; i< s_cfg.adc_num_readings; i++) {
        auto raw = adc1_get_raw(PIN_WATER_LEVEL_SENSE);
        level_voltage += esp_adc_cal_raw_to_voltage(raw, adc_chars);
    }
    level_voltage = level_voltage / s_cfg.adc_num_readings;

    ESP_LOGD(TAG, "Level voltage: %f", level_voltage);

    // Done, disable to prevent electrolysis
    gpio_set_level(PIN_WATER_LEVEL_ENABLE, 0);

    if (level_voltage > s_cfg.refill_voltage_threshold) {
        return false;
    } else {
        return true;
    }
}

void boiler_set_check_level_fn(check_level_fn fn) {

}


static void _start_refill() {
    // Open solenoid valve
    ESP_LOGI(TAG, "Opening refill solenoid");
    gpio_set_level(GPIO_TRIG2_REL2, 1);

    ESP_ERROR_CHECK(esp_event_post_to(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_STARTED, nullptr, 0,
                                      portMAX_DELAY));
}

static void _stop_refill() {
    // Turn pump off and close solenoid valve
    ESP_ERROR_CHECK(esp_event_post_to(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_STOPPED, nullptr, 0,
                                      portMAX_DELAY));

    ESP_LOGI(TAG, "Closing refill solenoid");
    gpio_set_level(GPIO_TRIG2_REL2, 0);
}

static void _refill_error() {
    bool state = (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U;
    if (state) {
        ESP_LOGE(TAG, "Sopping refill, running for too long");
        ESP_ERROR_CHECK(esp_event_post_to(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_ERROR, nullptr, 0,
                                          portMAX_DELAY));
        _stop_refill();
    }
}

void _state_unknown(bool level_ok, TickType_t now_ms) {
    if (level_ok) {
        current_state_fn = _state_not_filling;
    } else {
        current_state_fn = _state_filling;
    }

    s_refill_start_ms = 0;
    s_begin_ok_ms = 0;
    s_begin_low_ms = 0;

    // Invoke new state
    current_state_fn(level_ok, now_ms);
}

void _state_not_filling(bool level_ok, TickType_t now_ms) {
    ESP_LOGI(TAG, "Not filling");
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    if(s_refill_start_ms != 0) {
        _stop_refill();
        s_refill_start_ms = 0;
    }

    // Check hysteresis threshold
    if (!level_ok) {
        if (s_begin_low_ms == 0) {
            s_begin_low_ms = now_ms;
        }

        if ((now_ms - s_begin_low_ms) > s_cfg.level_low_hysteresis_ms) {
            current_state_fn = _state_filling;
        }
    } else {
        s_begin_low_ms = 0;
    }
    s_begin_ok_ms = 0;
}

void _state_filling(bool level_ok, TickType_t now_ms) {
    ESP_LOGI(TAG, "Filling");

    xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);
    // Turn on pump, but time it, cannot run forever
    if (s_refill_start_ms == 0) {
        _start_refill();
        s_refill_start_ms = now_ms;
    } else if (s_cfg.max_refill_time_ms > 0 && now_ms - s_refill_start_ms > s_cfg.max_refill_time_ms) {
        // Uh... something is going wrong here...
        current_state_fn = _state_error;
    }

    // Check hysteresis threshold
    if (level_ok) {
        if (s_begin_ok_ms == 0) {
            s_begin_ok_ms = now_ms;
        }

        if ((now_ms - s_begin_ok_ms) > s_cfg.level_ok_hysteresis_ms) {
            current_state_fn = _state_not_filling;
        }
    } else {
        s_begin_ok_ms = 0;
    }
    s_begin_low_ms = 0;
}

void _state_error(bool level_ok, TickType_t now_ms) {
    ESP_LOGE(TAG, "Refill error");

    if (s_refill_start_ms > 0) {
        _stop_refill();
        s_refill_start_ms = 0;
    }

    if (level_ok) {
        // Get out of error state, start over again
        current_state_fn = _state_unknown;
    }
}


static void _tick(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id != TICK) {
        return;
    }

    // Not running any of this in standby
    if (!(xEventGroupGetBits(status_event_group) &  POWER_ON_BIT)) {
        return;
    }

    // Don't run this until the machine finishes init basically, we get wrong level readings otherwise
    if (pdTICKS_TO_MS(xTaskGetTickCount()) < s_cfg.start_delay_ms) {
        s_refill_start_ms = 0;
        return;
    }

    TickType_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());

    // Read current boiler level but add hysteresis
    bool level_ok = check_level();
    current_state_fn(level_ok, now_ms);
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        // Always stop refill
        _stop_refill();
    } else if (id == POWER_ACTIVE) {
        // Start over again
        current_state_fn = _state_unknown;
    }
}

void boiler_refill_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
    current_state_fn = _state_unknown;
    check_level = boiler_check_level;

    _load_nvram();

    // Start with boiler not ok, until we can get a reading
    xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);

    // Output pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_TRIG2_REL2) |
            (1ULL << PIN_WATER_LEVEL_ENABLE)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Turn off outputs
    gpio_set_level(GPIO_TRIG2_REL2, 0);
    gpio_set_level(PIN_WATER_LEVEL_ENABLE, 0);

    // Configure ADC input
    // Configure pins for voltage divider
    ESP_LOGI(TAG, "Initialising ADC pin input");

    // Configure ADC
    auto attenuation = ADC_ATTEN_DB_11;
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(PIN_WATER_LEVEL_SENSE, attenuation);

    //Characterize ADC
    adc_chars = static_cast<esp_adc_cal_characteristics_t *>(calloc(1, sizeof(esp_adc_cal_characteristics_t)));
    esp_adc_cal_characterize(unit, attenuation, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);

    // We want tick events
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, TICK,
                                                    _tick, s_event_loop));


    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));

    ESP_LOGI(TAG, "Initialised");
}

void boiler_refill_delete() {
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, TICK, _tick));

    free(adc_chars);
}


// -----------------------------------------------------------------------------------------
// Config stuff
// -----------------------------------------------------------------------------------------

static void _load_nvram() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(BOILER_REFILL_NVS_CFG_STORE, NVS_READWRITE, &my_handle));

    nvram_store_get_u16(my_handle, KEY_start_delay_ms, (uint16_t *) &s_cfg.start_delay_ms,
                        (void *) &BOILER_REFILL_START_DELAY_MS_DEFAULT);
    nvram_store_get_u16(my_handle, KEY_stabilise_ms, (uint16_t *) &s_cfg.stabilise_ms,
                        (void *) &BOILER_REFILL_STABILISE_MS_DEFAULT);
    nvram_store_get_u16(my_handle, KEY_refill_voltage_threshold, (uint16_t *) &s_cfg.refill_voltage_threshold,
                        (void *) &BOILER_REFILL_REFILL_VOLTAGE_THRESHOLD_DEFAULT);
    nvram_store_get_u16(my_handle, KEY_max_refill_time_ms, (uint16_t *) &s_cfg.max_refill_time_ms,
                        (void *) &BOILER_REFILL_MAX_REFILL_TIME_MS_DEFAULT);
    nvram_store_get_u16(my_handle, KEY_adc_num_readings, (uint16_t *) &s_cfg.adc_num_readings,
                        (void *) &BOILER_REFILL_ADC_NUM_READINGS_DEFAULT);
    nvram_store_get_u16(my_handle, KEY_level_low_hysteresis_ms, (uint16_t *) &s_cfg.level_low_hysteresis_ms,
                        (void *) &BOILER_REFILL_LEVEL_LOW_HYSTERESIS_MS_DEFAULT);
    nvram_store_get_u16(my_handle, KEY_level_ok_hysteresis_ms, (uint16_t *) &s_cfg.level_ok_hysteresis_ms,
                        (void *) &BOILER_REFILL_LEVEL_OK_HYSTERESIS_MS_DEFAULT);

    nvs_close(my_handle);
}

static void _save_nvram() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(BOILER_REFILL_NVS_CFG_STORE, NVS_READWRITE, &my_handle));

    nvram_store_set_u16(my_handle, KEY_start_delay_ms , &s_cfg.start_delay_ms);
    nvram_store_set_u16(my_handle, KEY_stabilise_ms , &s_cfg.stabilise_ms);
    nvram_store_set_u16(my_handle, KEY_adc_num_readings , &s_cfg.adc_num_readings);
    nvram_store_set_u16(my_handle, KEY_refill_voltage_threshold , &s_cfg.refill_voltage_threshold);
    nvram_store_set_u16(my_handle, KEY_max_refill_time_ms , &s_cfg.max_refill_time_ms);
    nvram_store_set_u16(my_handle, KEY_level_low_hysteresis_ms, &s_cfg.level_low_hysteresis_ms);
    nvram_store_set_u16(my_handle, KEY_level_ok_hysteresis_ms, &s_cfg.level_ok_hysteresis_ms);

    nvs_close(my_handle);
}


const boiler_refill_cfg_t &boiler_refill_get_cfg() {
    return s_cfg;
}

void boiler_refill_update_cfg(const cJSON* json) {
    boiler_refill_cfg_t new_config = s_cfg;
    new_config.from_json(json);
    boiler_refill_set_cfg(new_config);
}

void boiler_refill_set_cfg(boiler_refill_cfg_t config) {
    if (config.start_delay_ms < 10000) {
        s_cfg.start_delay_ms = config.start_delay_ms;
    } else {
        ESP_LOGE(TAG, "start_delay_ms is out of bounds: %d, ignoring.", config.start_delay_ms);
    }

    if (config.stabilise_ms < 150) {
        s_cfg.stabilise_ms = config.stabilise_ms;
    } else {
        ESP_LOGE(TAG, "stabilise_ms is out of bounds: %d, ignoring.", config.stabilise_ms);
    }

    if (config.adc_num_readings < 128) {
        s_cfg.adc_num_readings = config.adc_num_readings;
    } else {
        ESP_LOGE(TAG, "adc_num_readings is out of bounds: %d, ignoring.", config.adc_num_readings);
    }

    if (config.refill_voltage_threshold >= 150 && config.refill_voltage_threshold < 2800) {
        s_cfg.refill_voltage_threshold = config.refill_voltage_threshold;
    } else {
        ESP_LOGE(TAG, "refill_voltage_threshold is out of bounds: %d, ignoring.", config.refill_voltage_threshold);
    }

    if (config.refill_voltage_threshold >= 150 && config.refill_voltage_threshold < 2800) {
        s_cfg.refill_voltage_threshold = config.refill_voltage_threshold;
    } else {
        ESP_LOGE(TAG, "refill_voltage_threshold is out of bounds: %d, ignoring.", config.refill_voltage_threshold);
    }

    if (config.max_refill_time_ms >= 1000 && config.max_refill_time_ms < 10000) {
        s_cfg.max_refill_time_ms = config.max_refill_time_ms;
    } else {
        ESP_LOGE(TAG, "max_refill_time_ms is out of bounds: %d, ignoring.", config.max_refill_time_ms);
    }

    if (config.level_low_hysteresis_ms < 2000) {
        s_cfg.level_low_hysteresis_ms = config.level_low_hysteresis_ms;
    } else {
        ESP_LOGE(TAG, "level_low_hysteresis_ms is out of bounds: %d, ignoring.", config.level_low_hysteresis_ms);
    }

    if (config.level_ok_hysteresis_ms < 2000) {
        s_cfg.level_ok_hysteresis_ms = config.level_ok_hysteresis_ms;
    } else {
        ESP_LOGE(TAG, "level_ok_hysteresis_ms is out of bounds: %d, ignoring.", config.level_ok_hysteresis_ms);
    }

    // Save what we can then
    _save_nvram();
}


void boiler_refill_reset_cfg() {
    nvs_handle my_handle;
    ESP_ERROR_CHECK(nvs_open(BOILER_REFILL_NVS_CFG_STORE, NVS_READWRITE, &my_handle));
    nvs_erase_all(my_handle);
    nvs_close(my_handle);

    _load_nvram();
}

const boiler_refill_status_t& boiler_refill_get_status() {
    return s_status;
}

void boiler_refill_reset_stats() {
    s_status.refill_error_count = 0;
}

