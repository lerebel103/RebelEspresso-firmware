#include <cstdint>

#include <FreeRTOS.h>
#include <freertos/task.h>
#include <hw/r1.0/hw_config.h>
#include <esp_log.h>
#include "rtds.h"

#define TAG "Temperature"

// This delay is required just after we've switched RTD port so VBias stabilises
#define MULTIPLEX_SWITCH_DELAY_MS 10

// Talks to the MAX IC for RTD sensing
static Max31865 s_tempSensor(GPIO_MISO, GPIO_MOSI, GPIO_SCK, GPIO_RTD_CS);
static max31865_rtd_config_t s_rtdConfig = {};

/**
 * Contains our last known reading
 */
static rtd_data_t _rtd_array[RTD_MAX_COUNT];

static void _read_temp(struct rtd_data_t *data) {
    static uint16_t rtd;
    s_tempSensor.clearFault();
    s_tempSensor.getRTD(&rtd, &data->fault);
    if (data->fault == Max31865Error::NoError) {
        data->temperature = Max31865::RTDtoTemperature(rtd, s_rtdConfig);
    }
}

void rtds_update() {
    uint8_t idx = 0;

    gpio_set_level(GPIO_RTD_A0, 0);
    gpio_set_level(GPIO_RTD_A1, 0);
    vTaskDelay(pdMS_TO_TICKS(MULTIPLEX_SWITCH_DELAY_MS));
    _read_temp(&_rtd_array[idx++]);

    gpio_set_level(GPIO_RTD_A0, 0);
    gpio_set_level(GPIO_RTD_A1, 1);
    vTaskDelay(pdMS_TO_TICKS(MULTIPLEX_SWITCH_DELAY_MS));
    _read_temp(&_rtd_array[idx++]);

    gpio_set_level(GPIO_RTD_A0, 1);
    gpio_set_level(GPIO_RTD_A1, 0);
    vTaskDelay(pdMS_TO_TICKS(MULTIPLEX_SWITCH_DELAY_MS));
    _read_temp(&_rtd_array[idx++]);

    gpio_set_level(GPIO_RTD_A0, 1);
    gpio_set_level(GPIO_RTD_A1, 1);
    vTaskDelay(pdMS_TO_TICKS(MULTIPLEX_SWITCH_DELAY_MS));
    _read_temp(&_rtd_array[idx++]);
}

esp_err_t rtds_get(rtd_data_t* data, uint8_t idx) {
    if (idx >= RTD_MAX_COUNT) {
        ESP_LOGE(TAG, "RTD index is out of range");
        return ESP_FAIL;
    } else {
        *data = _rtd_array[idx];
        return ESP_OK;
    }
}


int rtds_init(const rtds_cfg_t *cfg) {
    // Initialise multiplexer
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = ((1ULL << GPIO_RTD_A0) | (1ULL << GPIO_RTD_A1));

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Now for the RTC IC
    max31865_config_t tempConfig = {};
    tempConfig.autoConversion = false;
    tempConfig.faultDetection = Max31865FaultDetection::AutoDelay;
    tempConfig.vbias = true;
    tempConfig.filter = Max31865Filter::Hz50;
    tempConfig.nWires = Max31865NWires::Two;

    s_rtdConfig.nominal = 1000.0f;
    s_rtdConfig.ref = 4020.0f;

    ESP_ERROR_CHECK(s_tempSensor.begin(tempConfig));

    // Based on upper value of 1K at around -10C and +200C
    auto min_rtd = (1U << 15U) * 500 / s_rtdConfig.ref;
    auto max_rtd =(1U << 15U) * 2000 / s_rtdConfig.ref;
    ESP_ERROR_CHECK(s_tempSensor.setRTDThresholds(min_rtd, max_rtd));

    max31865_config_t read_cfg;
    s_tempSensor.getConfig(&read_cfg);

    if (read_cfg.filter != tempConfig.filter ||
        read_cfg.nWires != tempConfig.nWires) {
        ESP_LOGE(TAG, "Failed to initialised RTD sensor.");
        return -1;
    } else {
        ESP_LOGI(TAG, "RTD sensor initialised.");
        return 0;
    }
}
