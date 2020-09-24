#include <cstdint>
#include <hw/r1.0/hw_config.h>
#include <esp_log.h>
#include "rtds.h"

#define TAG "Temperature"

// Talks to the MAX IC for RTD sensing
static Max31865 s_tempSensor(GPIO_MISO, GPIO_MOSI, GPIO_SCK, GPIO_RTD_CS);
static max31865_rtd_config_t s_rtdConfig = {};


static void _read_temp(struct rtd_data_t *data) {
    uint16_t rtd;
    s_tempSensor.clearFault();
    s_tempSensor.getRTD(&rtd, &data->fault);

    //s_tempSensor.getRTD(&rtd, &data->fault);
    //data->rtd_val = rtd; //(rtd * s_rtdConfig.ref) / (1U << 15U);
}

void rtds_read_1(const rtds_cfg_t *cfg, struct rtd_data_t *data) {
    _read_temp(data);
}

void rtds_read_2(const rtds_cfg_t *cfg, struct rtd_data_t *data) {
    _read_temp(data);
}

void rtds_read_3(const rtds_cfg_t *cfg, struct rtd_data_t *data) {
    _read_temp(data);
}

void rtds_read_4(const rtds_cfg_t *cfg, struct rtd_data_t *data) {
    _read_temp(data);
}

int rtds_init(const rtds_cfg_t *cfg) {
    // Turn off logging, it will break ISR handling otherwise
    esp_log_level_set("Max31865", ESP_LOG_ERROR);

    max31865_config_t tempConfig = {};
    tempConfig.autoConversion = false;
    tempConfig.vbias = true;
    tempConfig.filter = Max31865Filter::Hz50;
    tempConfig.nWires = Max31865NWires::Two;

    s_rtdConfig.nominal = 1000.0f;
    s_rtdConfig.ref = 4020.0f;

    ESP_ERROR_CHECK(s_tempSensor.begin(tempConfig));
    ESP_ERROR_CHECK(s_tempSensor.setRTDThresholds(0x2000, 0x2500));

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
