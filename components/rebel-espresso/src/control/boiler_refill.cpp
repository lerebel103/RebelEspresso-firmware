#include <hal/gpio_types.h>
#include <hw_config.h>
#include <esp_log.h>
#include "boiler_refill.h"
#include <esp_adc_cal.h>

#define DEFAULT_VREF    1100

const static char* TAG = "refill";

static const adc_unit_t unit = ADC_UNIT_1;
static esp_adc_cal_characteristics_t *adc_chars;

void boiler_refill_init() {
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

    ESP_LOGI(TAG, "Getting calibration");
    //Characterize ADC
    adc_chars = static_cast<esp_adc_cal_characteristics_t *>(calloc(1, sizeof(esp_adc_cal_characteristics_t)));
    esp_adc_cal_characterize(unit, attenuation, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);

    ESP_LOGI(TAG, "Initialised");


    auto raw = adc1_get_raw(PIN_WATER_LEVEL_SENSE);
    auto level_voltage = esp_adc_cal_raw_to_voltage(raw, adc_chars);

    ESP_LOGI(TAG, "Level voltage: %d", level_voltage);
}

