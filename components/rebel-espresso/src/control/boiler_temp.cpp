#include <hw/rtds.h>
#include <esp_log.h>
#include <driver/rmt.h>
#include <cmath>
#include <src/events.h>
#include <esp_event.h>
#include "boiler_temp.h"
#include "rmt_duty_map.h"
#include "window.h"

#define TAG "Boiler"

#define RMT_CLK_DIV 160
#define OVERT_TEMP_THRESHOLD 10
#define RMT_TX_CHANNEL RMT_CHANNEL_0


struct boiler_temp_cfg_t {
    uint8_t mains_hz = MAINS_50HZ;
    pid_setpoint_t setpoint = {};
    pid_cfg_t pid;
};

static esp_event_loop_handle_t s_event_loop;
static boiler_temp_cfg_t s_cfg;
static window_handle_t s_data_window;
static double g_last_pid_err = 0;

static uint64_t s_last_time_us = 0;
static uint64_t s_last_duty = 0;

/*
 * Apply new duty to SSR
 *
 * @param duty integral [0-100]
 */
extern "C" void boiler_temp_set_duty(int duty) {
    const struct rmt_pulse_t * pulses = rmt_duty_get_pulses(duty, s_cfg.mains_hz);
    ESP_ERROR_CHECK(rmt_fill_tx_items(RMT_TX_CHANNEL, pulses->items, pulses->num_items, false));
    s_last_duty = duty;
}

/**
 * Used for testing
 */
extern "C" uint8_t boiler_temp_get_duty() {
    return s_last_duty;
}


/*
 * Turns power off immediately to ssr
 */
static void _power_off_ssr() {
    // Turn off RMT and force pin to zero as safety
    boiler_temp_set_duty(0);
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
    boiler_temp_set_duty(0);
    rmt_set_tx_loop_mode(config.channel, true);
}

static void _pid_reset() {
    ESP_LOGD(TAG, "Resetting...");
    s_last_time_us = 0;
    g_last_pid_err = 0;
    window_reset(&s_data_window);
    ESP_LOGD(TAG, "Reset done.");
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        ESP_LOGI(TAG, "Powering down Boiler SSR");
        _power_off_ssr();
    } else if (id == POWER_ACTIVE) {
        ESP_LOGI(TAG, "Resuming Boiler SSR");
        _pid_reset();
    }
}


void boiler_temp_tick(uint64_t time_us, const rtd_data_t &data) {
    if (!(xEventGroupGetBits(status_event_group) &  POWER_ON_BIT)) {
        ESP_LOGW(TAG, "In standby, not running.");
        _power_off_ssr();
        return;
    } else  if (!(xEventGroupGetBits(status_event_group) &  BOILER_LEVEL_OK_BIT)) {
        ESP_LOGW(TAG, "Not running, boiler level low");
        _power_off_ssr();
        return;
    } else if (data.fault != Max31865Error::NoError) {
        ESP_LOGE(TAG, "Boiler sensor error %s", Max31865::errorToString(data.fault));
        _power_off_ssr();
        return;
    }

    // If we've had a gap, reset the PID
    double deltaT = (double)(time_us - s_last_time_us) / 1e6;
    if (deltaT >= 5) {
        _pid_reset();
    } else {
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
            if (s_last_time_us != 0 && deltaT != 0) {
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
            boiler_temp_set_duty(duty);
            g_last_pid_err = error;
        }
    }

    s_last_time_us = time_us;
}


void boiler_temp_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
    s_cfg.setpoint.temp_max = 120;
    window_init(&s_data_window);
    _rmt_tx_init();

    // Get our power events in place so we can run the process loop as needed
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));
}

void boiler_temp_delete() {
    rmt_driver_uninstall(RMT_TX_CHANNEL);
    window_reset(&s_data_window);
}
