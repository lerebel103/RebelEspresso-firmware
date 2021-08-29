#include <esp_adc_cal.h>
#include <src/hw/base/out_signals.h>
#include "hw_specs.h"

#include "hw_config.h"
#include "boiler_temp.h"
#include "brew_temp.h"
#include "brew_tec.h"
#include "ready_indicator.h"

#define DEFAULT_VREF                1100
static const adc_unit_t unit = ADC_UNIT_1;
static esp_adc_cal_characteristics_t *adc_chars;

void hw_specs_read_water_level_mv(uint8_t* status, double* value) {
    static int num_readings = 25;

    // Let things stabilise
    vTaskDelay(pdMS_TO_TICKS(5));

    double level_voltage = 0.0;
    for (int i = 0; i < 25; i++) {
        auto raw = adc1_get_raw(PIN_WATER_LEVEL_SENSE);
        level_voltage += esp_adc_cal_raw_to_voltage(raw, adc_chars);
        ets_delay_us(250);
    }

    *value = level_voltage / num_readings;
    printf("\r\nwater level %f\r\n", *value);
}


void hw_specs_init(esp_event_loop_handle_t event_loop) {
    out_signals_init();

    // Configure ADC
    auto attenuation = ADC_ATTEN_DB_11;
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(PIN_WATER_LEVEL_SENSE, attenuation);

    //Characterize ADC
    adc_chars = static_cast<esp_adc_cal_characteristics_t *>(calloc(1, sizeof(esp_adc_cal_characteristics_t)));
    esp_adc_cal_characterize(unit, attenuation, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);

    brew_tec_init(event_loop);
    ready_indicator_init(event_loop);
}

void hw_specs_cfg_to_json(cJSON *root, const char *base_key) {
    auto brew_cfg = brew_tec_get_cfg();
    brew_cfg.to_json(root, base_key);

    auto ready_indicator_cfg = ready_indicator_get_cfg();
    ready_indicator_cfg.to_json(root, base_key);

}

void hw_specs_status_to_json(cJSON *root, const char *base_key) {
    auto brew_status = brew_tec_get_status();
    brew_status.to_json(root, base_key);

    auto ready_indicator_status = ready_indicator_get_status();
    ready_indicator_status.to_json(root, base_key);

}

void hw_specs_handle_new_cfg(const cJSON *cfg) {
    brew_tec_update_cfg(cfg);
    ready_indicator_update_cfg(cfg);

}

void hw_specs_handle_new_temp(uint64_t time_us, const reading_t &data, uint8_t idx) {
    switch (idx) {
        case RTD_BREW_BOILER_IDX:
            boiler_temp_process(time_us, data);
            break;
        case RTD_BREW_HEAD_IDX:
            brew_tec_process(time_us, data);
            brew_temp_process(time_us, data);
            ready_indicator_process(time_us, data);
            break;
        case RTD_TEC_HOT_IDX:
            brew_tec_hot_updated(time_us, data);
            break;
        case RTD_TEC_COLD_IDX:
            brew_tec_cold_updated(time_us, data);
            break;
    }
}
