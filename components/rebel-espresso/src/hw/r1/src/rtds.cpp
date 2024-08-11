#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <Max31865.h>

#include "hw_config.h"
#include "events.h"
#include "rtds.h"

#define TAG "Temperature"

// The analogue switch at the front of the max31965 introduces
// a small resistance, as per data sheet, this is as a correction offset.
#define STATIC_R_OFFSET (0.6)

// Talks to the MAX IC for RTD sensing
static Max31865 s_tempSensor(PIN_MISO, PIN_MOSI, PIN_SCK, PIN_OUT_ADC_CS);
static max31865_rtd_config_t s_rtdConfig = {};
static uint16_t s_min_rtd = 0;
static uint16_t s_max_rtd = 0;
static max31865_config_t s_tempConfig;

/**
 * Contains our last known reading
 */
static reading_t _rtd_array[RTD_MAX_COUNT];

static void _read_temp(rtd_update_cb_t cb, int idx) {
    uint16_t rtd;

    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(s_tempSensor.setConfig(s_tempConfig));
    ESP_ERROR_CHECK(s_tempSensor.setRTDThresholds(s_min_rtd, s_max_rtd));
    Max31865Error fault;
    s_tempSensor.getRTD(&rtd, &fault);

    if (fault == Max31865Error::NoError) {
        _rtd_array[idx].fault = RTD_NoError;
    } else if (fault == Max31865Error::Voltage) {
        _rtd_array[idx].fault = RTD_Voltage;
    } else if (fault == Max31865Error::RTDInLow) {
        _rtd_array[idx].fault = RTD_InLow;
    } else if (fault == Max31865Error::RefLow) {
        _rtd_array[idx].fault = RTD_RefLow;
    } else if (fault == Max31865Error::RefHigh) {
        _rtd_array[idx].fault = RTD_RefHigh;
    } else if (fault == Max31865Error::RTDLow) {
        _rtd_array[idx].fault = RTD_RTDLow;
    } else if (fault == Max31865Error::RTDHigh) {
        _rtd_array[idx].fault = RTD_RTDHigh;
    }

    // Calculate new value if we can, otherwise leave the old one there.
    if (_rtd_array[idx].fault == RTD_NoError) {
        _rtd_array[idx].value = Max31865::RTDtoTemperature(rtd, s_rtdConfig);
    }

    // Invoke CB now
    cb(esp_timer_get_time(), _rtd_array[idx], idx);
}

void rtds_update(rtd_update_cb_t cb) {
    // Select a port a time and read temp from it
    gpio_set_level(PIN_OUT_RTD_A1, 0);
    gpio_set_level(PIN_OUT_RTD_A0, 0);
    _read_temp(cb, RTD_BREW_BOILER_IDX);

    gpio_set_level(PIN_OUT_RTD_A0, 1);
    _read_temp(cb, RTD_BREW_HEAD_IDX);

    gpio_set_level(PIN_OUT_RTD_A1, 1);
    _read_temp(cb, RTD_TEC_COLD_IDX);

    gpio_set_level(PIN_OUT_RTD_A0, 0);
    _read_temp(cb, RTD_TEC_HOT_IDX);

    // Always trigger display refresh at the back of new temperatures
    xEventGroupSetBits(status_event_group, REFRESH_DISPLAY_BIT);
}

esp_err_t rtds_get(reading_t* data, uint8_t idx) {
    if (idx >= RTD_MAX_COUNT) {
        ESP_LOGE(TAG, "RTD index is out of range");
        return ESP_FAIL;
    } else {
        *data = _rtd_array[idx];
        return ESP_OK;
    }
}

int rtds_init(spi_host_device_t spi, const rtds_cfg_t *cfg) {
    // Initialise multiplexer
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = ((1ULL << PIN_OUT_RTD_A0) | (1ULL << PIN_OUT_RTD_A1));

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Now for the RTC IC
    s_tempConfig = {};
    s_tempConfig.autoConversion = false;
    s_tempConfig.faultDetection = Max31865FaultDetection::NoAction;
    s_tempConfig.vbias = true;
    s_tempConfig.filter = Max31865Filter::Hz50;
    s_tempConfig.nWires = Max31865NWires::Two;

    s_rtdConfig.nominal = RTD_R_NOMINAL;
    s_rtdConfig.ref = RTD_R_REF;
    s_rtdConfig.offsetOhms = STATIC_R_OFFSET;

    ESP_ERROR_CHECK(s_tempSensor.begin(s_tempConfig));

    // Based on upper value of 1K at around -10C and +200C
    s_min_rtd = (1U << 15U) * 500 / s_rtdConfig.ref;
    s_max_rtd =(1U << 15U) * 2000 / s_rtdConfig.ref;
    ESP_ERROR_CHECK(s_tempSensor.setRTDThresholds(s_min_rtd, s_max_rtd));

    max31865_config_t read_cfg;
    s_tempSensor.getConfig(&read_cfg);

    if (read_cfg.filter != s_tempConfig.filter ||
        read_cfg.nWires != s_tempConfig.nWires) {
        ESP_LOGE(TAG, "Failed to initialised RTD sensor.");
        return -1;
    } else {
        ESP_LOGI(TAG, "RTD sensor initialised.");
        // Turn off logs from max RTD IC
        //esp_log_level_set("Max31865", ESP_LOG_NONE);
        return 0;
    }
}
