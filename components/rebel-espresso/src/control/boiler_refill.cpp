#include <hal/gpio_types.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <hw_config.h>
#include <esp_log.h>
#include "boiler_refill.h"
#include <esp_adc_cal.h>
#include <src/events.h>
#include <esp_event.h>

#define DEFAULT_VREF                1100

const static char* TAG = "refill";

struct boiler_refill_cfg_t {

    /**
     * Initial wait time before starting the check.
     * This is needed as the system is still initialising and we get a false reading
     */
    uint16_t stabilise_ms = 10;

    /**
     * Readings are averaged, how many to take in succession.
     */
    uint16_t num_readings = 24;

    /**
     * Theshold over which we decice that the boiler is empty
     */
    uint16_t refill_voltage_threshold = 1500;

    /**
     * Cap refill time and raise error if time is exceeded
     */
    uint16_t max_refill_time = 5000;
};

static boiler_refill_cfg_t s_cfg;
static const adc_unit_t unit = ADC_UNIT_1;
static esp_adc_cal_characteristics_t *adc_chars;
TickType_t s_refill_start_ms = 0;
static esp_event_loop_handle_t s_event_loop;

bool boiler_refill_read_state() {
    // Enable voltage on probe
    gpio_set_level(PIN_WATER_LEVEL_ENABLE, 1);
    vTaskDelay(pdMS_TO_TICKS(s_cfg.stabilise_ms));

    double level_voltage = 0;
    for (int i=0; i< s_cfg.num_readings; i++) {
        auto raw = adc1_get_raw(PIN_WATER_LEVEL_SENSE);
        level_voltage += esp_adc_cal_raw_to_voltage(raw, adc_chars);
    }
    level_voltage = level_voltage / s_cfg.num_readings;

    ESP_LOGD(TAG, "Level voltage: %f", level_voltage);

    // Done, disable to prevent electrolysis
    gpio_set_level(PIN_WATER_LEVEL_ENABLE, 0);

    if (level_voltage > s_cfg.refill_voltage_threshold) {
        return false;
    } else {
        return true;
    }
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

static void _tick(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id != TICK) {
        return;
    }

    // Not running any of this in standby
    if (!(xEventGroupGetBits(status_event_group) &  POWER_ON_BIT)) {
        return;
    }

    // Don't run this until the machine finishes init basically, we get wrong level readings otherwise
    if (pdTICKS_TO_MS(xTaskGetTickCount()) < 1000) {
        s_refill_start_ms = 0;
        return;
    }

    // disable check for now
    bool level_ok = true; //boiler_refill_read_state();

    if (!level_ok) {
        xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);

        // Turn on pump, but time it, cannot run forever
        TickType_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
        if (s_refill_start_ms == 0) {
            _start_refill();
            s_refill_start_ms = now_ms;
        } else if (s_cfg.max_refill_time > 0 && now_ms - s_refill_start_ms > s_cfg.max_refill_time) {
            // Uh... something is going wrong here...
            _refill_error();
        }
    } else if (s_refill_start_ms > 0) {
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        _stop_refill();
        s_refill_start_ms = 0;
    } else {
        // All is well then
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    }
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        // Always stop refill
        _stop_refill();
    } else if (id == POWER_ACTIVE) {
    }
}

void boiler_refill_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;

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

