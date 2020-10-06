#include <hw/rtds.h>
#include <esp_log.h>
#include <driver/rmt.h>
#include <cmath>
#include "boiler.h"
#include "rmt_duty_map.h"
#include "window.h"

#define TAG "Boiler"

#define RMT_CLK_DIV 160
#define OVERT_TEMP_THRESHOLD 10
#define RMT_TX_CHANNEL RMT_CHANNEL_0


struct boiler_cfg_t {
    uint8_t mains_hz = MAINS_50HZ;
    pid_setpoint_t setpoint = {};
    pid_cfg_t pid;
};


static bool s_enabled = false;
static boiler_cfg_t s_cfg;
static window_handle_t s_data_window;
static double g_last_pid_err = 0;

static uint64_t s_last_time_us = 0;
static uint64_t s_last_duty = 0;

/*
 * Apply new duty to SSR
 *
 * @param duty integral [0-100]
 */
extern "C" void boiler_set_duty(int duty) {
    const struct rmt_pulse_t * pulses = rmt_duty_get_pulses(duty, s_cfg.mains_hz);
    ESP_ERROR_CHECK(rmt_fill_tx_items(RMT_TX_CHANNEL, pulses->items, pulses->num_items, false));
    s_last_duty = duty;
}

/**
 * Used for testing
 */
extern "C" uint8_t boiler_get_duty() {
    return s_last_duty;
}


/*
 * Turns power off immediately to ssr
 */
static void _power_off_ssr() {
    // Turn off RMT and force pin to zero as safety
    boiler_set_duty(0);
    rmt_tx_stop(RMT_TX_CHANNEL);
    gpio_set_level(BOILER_SSR_PIN, 0);
}

/*
 * Initialize the RMT Tx channel
 */
static void _rmt_tx_init() {
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(BOILER_SSR_PIN, RMT_TX_CHANNEL);

    // Disable carrier and enable loop back so we can generate pulses
    config.tx_config.carrier_en = false;
    config.tx_config.loop_en = false;
    config.tx_config.idle_output_en = true;

    // set the maximum clock divider to be able to output
    // RMT pulses in range of about one hundred milliseconds
    config.clk_div = RMT_CLK_DIV;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // Set zero duty and enable loop so we continuously tx the last duty pulses
    boiler_set_duty(0);
    rmt_set_tx_loop_mode(config.channel, true);
}

static void _pid_reset() {
    ESP_LOGI(TAG, "Resetting...");
    s_last_time_us = 0;
    g_last_pid_err = 0;
    window_reset(&s_data_window);
    ESP_LOGI(TAG, "Reset done.");
}


void boiler_tick(uint64_t time_us, const rtd_data_t &data) {
    if (!s_enabled) {
        return;
    }

    double deltaT = (double)(time_us - s_last_time_us) / 1e6;
    if (data.fault == Max31865Error::NoError && (deltaT < 5 || s_last_time_us == 0)) {
        // Good to go
        ESP_LOGI(TAG, "Boiler temp=%f, deltaT=%fs", data.temperature, deltaT);

        // Accumulate
        window_accumulate(&s_data_window, time_us, &data, &s_cfg.setpoint, s_cfg.pid.I_reset_s * 1e3);

        // Get window statistics
        static window_data_t wdata = {};
        window_data(&s_data_window, &wdata);

        // delta from set-point, e.g. our error
        double error = s_cfg.setpoint.temp_max - data.temperature;

        // Safety. If we are 10 degrees over set temperature, cut off
        if (-error > OVERT_TEMP_THRESHOLD) {
            _power_off_ssr();
        } else {
            // Then we can proceed
            // Derivative part
            double derivative = 0;
            if (deltaT != 0) {
                derivative = (error - g_last_pid_err) / deltaT;
            }

            // Calculate duty, start with P and D
            double duty = (s_cfg.pid.P * error) + (s_cfg.pid.D * derivative);

            // Integral is added if we are below our delta error temp
            if (fabs(error) < s_cfg.pid.I_reset_temp) {
                duty += (s_cfg.pid.I * wdata.error_integral);
            } else {
                // Keep on resetting window in this case
                window_reset(&s_data_window);
            }

            ESP_LOGI(TAG, "Calculated PID duty %f", duty);
            boiler_set_duty(duty);
            g_last_pid_err = error;
        }

        s_last_time_us = time_us;
    } else {
        ESP_LOGE(TAG, "Boiler sensor error %s", Max31865::errorToString(data.fault));
        _power_off_ssr();
        _pid_reset();
    }
}

void boiler_enable(bool enable) {
    if (enable != s_enabled) {
        if (enable) {
            ESP_LOGI(TAG, "Enabled.");
            _pid_reset();

            // We don't need to set anything to re-enable RMT, the next tx will
            // re-start it in a loop
        } else {
            _power_off_ssr();
            ESP_LOGI(TAG, "Disabled");
        }
        s_enabled = enable;
    }
}

bool boiler_is_enabled() {
    return s_enabled;
}


void boiler_init() {
    s_cfg.setpoint.temp_max = 120;
    window_init(&s_data_window);
    _rmt_tx_init();
}

void boiler_delete() {
    rmt_driver_uninstall(RMT_TX_CHANNEL);
    window_reset(&s_data_window);
}
