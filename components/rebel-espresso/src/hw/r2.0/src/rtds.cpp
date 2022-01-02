#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/spi_master.h>
#include <cmath>

#include "hw_config.h"
#include "events.h"
#include "rtds.h"
#include "ADS124S08.h"

#define TAG "RTDS"
#define TEMP_RANGE_MIN -5
#define TEMP_RANGE_MAX 200

static constexpr float RTD_A = 3.9083e-3;
static constexpr float RTD_B = -5.775e-7;
static constexpr float A[6] = {-242.02, 2.2228, 2.5859e-3,
                               4.8260e-6, 2.8183e-8, 1.5243e-10};

static double _rtd_to_celcius(double vref, double value, double rtd_nominal) {
    double RNominal = rtd_nominal;

    double Rrtd = value / (vref / (2*ADC_RREF) );

    double Z1, Z2, Z3, Z4, temperature;
    Z1 = -RTD_A;
    Z2 = RTD_A * RTD_A - (4 * RTD_B);
    Z3 = (4 * RTD_B) / RNominal;
    Z4 = 2 * RTD_B;
    temperature = Z2 + (Z3 * Rrtd);
    temperature = (sqrt(temperature) + Z1) / Z4;

    if (temperature > 0.0) {
        return temperature;
    }

    Rrtd /= RNominal;
    Rrtd *= 100.0;
    return A[0] + A[1] * Rrtd + A[2] * pow(Rrtd, 2) + A[3] * pow(Rrtd, 3) +
           A[4] * pow(Rrtd, 4) + A[5] * pow(Rrtd, 5);
}


/**
 * Contains our last known reading
 */
static reading_t _rtd_array[RTD_MAX_COUNT];

void _set_reading(int idx, const ADS124S08_data_t &result, double reading) {
    // Easy with fault, just look at range bounds and error status from ADC
    if (result.status != 0) {
        _rtd_array[idx].fault = RTD_RefLow;
    } else if (reading <= TEMP_RANGE_MIN) {
        _rtd_array[idx].fault = RTD_RTDLow;
    } else if (reading >= TEMP_RANGE_MAX) {
        _rtd_array[idx].fault = RTD_RTDHigh;
    } else {
        _rtd_array[idx].fault = RTD_NoError;
    }

    _rtd_array[idx].value = reading;
}

static void _read_temp(rtd_update_cb_t cb, int idx) {
    ADS124S08_adc_mux_t adc_mux = {};
    ADS124S08_idac_mux_t idac_mux = {};

    /*
     * Table for RTD calculated in excel
     *
                    RTD1000	RTD100
        RtdMax	    1754	175.4
        RTdMin	    1000	100
        Gain	    2	    16
        iDac (uA)	250	    500
        Rref	    2000	2000
        VRTDMax	    0.4385	0.0877
        VRTDMin	    0.25	0.05
        VAINNLIM 	1	    2
        VAINPLIM 	1.4385	2.0877
        VIdacMax	1.6885	2.5877

        Delta	    0.4385	0.0877
        Corrected	0.877	1.4032
     */
    // Conclusion
    //      RTD1000: 250uA and Gain 2
    //      RTD100:  500uA and Gain 16
    double RTD_nominal = 1000;
    uint8_t idac_current = ADS124S08_IDAC_250uA;
    uint8_t pga_gain = ADS124S08_PGA_GAIN2;

    // Setup muxes for the right combo of RTD reads
    if (idx == RTD_BREW_BOILER_IDX) {
        adc_mux.mux_n = ADS124S08_MUX_AIN2;
        adc_mux.mux_p = ADS124S08_MUX_AIN1;
        idac_mux.mux_idac1 = ADS124S08_MUX_AIN0;
        idac_mux.mux_idac2 = ADS124S08_MUX_AIN3;

    } else if (idx == RTD_BREW_HEAD_IDX) {
        adc_mux.mux_n = ADS124S08_MUX_AIN6;
        adc_mux.mux_p = ADS124S08_MUX_AIN5;
        idac_mux.mux_idac1 = ADS124S08_MUX_AIN4;
        idac_mux.mux_idac2 = ADS124S08_MUX_AIN7;

    }
    if (idx == RTD_STEAM_BOILER_IDX) {
        adc_mux.mux_n = ADS124S08_MUX_AIN10;
        adc_mux.mux_p = ADS124S08_MUX_AIN9;
        idac_mux.mux_idac1 = ADS124S08_MUX_AIN8;
        idac_mux.mux_idac2 = ADS124S08_MUX_AIN11;

    }

    // Set excitation current accordingly if we have an RTD100 instead of RTD1000 attached
    if (RTD_nominal == 100) {
        idac_current = ADS124S08_IDAC_500uA;
        pga_gain = ADS124S08_PGA_GAIN16;
    }

    // Do a read
    auto result = ADS124S08_conv(true, ADS124S08_ref_EXTERNAL, adc_mux, idac_mux, idac_current, pga_gain);
    auto reading = _rtd_to_celcius(result.v_ref, result.value, RTD_nominal);
    _set_reading(idx, result, reading);

    // Invoke CB now
    cb(esp_timer_get_time(), _rtd_array[idx], idx);
}

void rtds_update(rtd_update_cb_t cb) {
    // Internal temperature
    ADS124S08_data_t reading = ADS124S08_internal_temp();
    _set_reading(RTD_INTERNAL_IDX, reading, reading.value);
    cb(esp_timer_get_time(), _rtd_array[RTD_INTERNAL_IDX], RTD_INTERNAL_IDX);

    // External RTDs
    _read_temp(cb, RTD_BREW_BOILER_IDX);
    _read_temp(cb, RTD_BREW_HEAD_IDX);
    _read_temp(cb, RTD_STEAM_BOILER_IDX);

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
