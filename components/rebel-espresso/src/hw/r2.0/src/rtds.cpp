#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/spi_master.h>

#include "hw_config.h"
#include "events.h"
#include "rtds.h"
#include "ADS124S08.h"

#define TAG "RTDS"

// The analogue switch at the front of the max31965 introduces
// a small resistance, as per data sheet, this is as a correction offset.
#define STATIC_R_OFFSET (0.6)

// Talks to the MAX IC for RTD sensing
static uint16_t s_min_rtd = 0;
static uint16_t s_max_rtd = 0;


/**
 * Contains our last known reading
 */
static reading_t _rtd_array[RTD_MAX_COUNT];

static void _read_temp(rtd_update_cb_t cb, int idx) {
    // TODO

    // Invoke CB now
    cb(esp_timer_get_time(), _rtd_array[idx], idx);
}

void rtds_update(rtd_update_cb_t cb) {

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
    ADS124S08_init(spi);
    ESP_LOGI(TAG, "ADC initialised");

    return ESP_OK;
}
