#include <cstdint>
#include <hw/rtds.h>
#include <esp_log.h>
#include <hw/r1.0/hw_config.h>
#include <driver/rmt.h>
#include "boiler.h"
#include "rmt_duty_map.h"

#define TAG "Boiler"

#define RMT_TX_CHANNEL RMT_CHANNEL_0
#define RMT_CLK_DIV 160

struct boiler_cfg_t {
    uint8_t mains_hz = MAINS_50HZ;
};


static boiler_cfg_t s_cfg;

static uint64_t s_last_time_us = 0;


/**
 * Apply new duty to SSR
 *
 * @param duty integral [0-100]
 */
static void _set_duty(uint8_t duty) {
    ESP_LOGW(TAG, "Setting new duty %d", duty);

    const struct rmt_pulse_t * pulses = rmt_duty_get_pulses(duty, s_cfg.mains_hz);
    ESP_ERROR_CHECK(rmt_fill_tx_items(RMT_TX_CHANNEL, pulses->items, pulses->num_items, false));
}

/*
 * Initialize the RMT Tx channel
 */
static void _rmt_tx_init()
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(GPIO_TRIG2_REL2, RMT_TX_CHANNEL);

    // Disable carrier and enable loop back so we can generate pulses
    config.tx_config.carrier_en = false;
    config.tx_config.loop_en = true;

    // set the maximum clock divider to be able to output
    // RMT pulses in range of about one hundred milliseconds
    config.clk_div = RMT_CLK_DIV;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));
}

static uint8_t count = 0;
static uint8_t last_duty;


void boiler_tick(uint64_t time_us, const rtd_data_t &data) {
    if (data.fault == Max31865Error::NoError) {
        // Good to go
        uint64_t deltaT = time_us - s_last_time_us;
        ESP_LOGI(TAG, "Boiler temp=%f, deltaT=%lld", data.temperature, deltaT);

        if (count % 1 == 0) {
            last_duty += 1;
            if (last_duty > 100) {
                last_duty = 0;
            }

            last_duty += 1;

            _set_duty(last_duty);
        }


        count ++;


        s_last_time_us = time_us;
    } else {
        ESP_LOGE(TAG, "Boiler sensor error %s", Max31865::errorToString(data.fault));
    }
}


void boiler_init() {
    _rmt_tx_init();

    // Set zero duty
    _set_duty(0);
    
}
