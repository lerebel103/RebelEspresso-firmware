#include <cstdint>
#include <hw/rtds.h>
#include <esp_log.h>
#include <hal/ledc_types.h>
#include <driver/ledc.h>
#include <src/hw/r1.0/hw_config.h>
#include <freertos/task.h>
#include <src/events.h>
#include <esp_event.h>
#include "brew_temp.h"

#define TAG "BrewHead"

static uint64_t s_last_time_us = 0;
static ledc_channel_config_t s_pwm_channel;
static esp_event_loop_handle_t s_event_loop;
static int duty = 0;

static void _set_duty(int duty) {

    if (gpio_get_level(GPIO_HBRIDGE_SO)) {
        gpio_set_level(GPIO_HBRIDGE_DIS, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGW("Hbridge", "Got error from hbridge, turning off and on");
        gpio_set_level(GPIO_HBRIDGE_DIS, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ledc_set_duty(s_pwm_channel.speed_mode, s_pwm_channel.channel, (uint32_t) (1024 * duty / 100.0f));
    ledc_update_duty(s_pwm_channel.speed_mode, s_pwm_channel.channel);
    ESP_LOGI("Hbridge", "Duty set to %d", duty);
}


void brew_temp_process(uint64_t time_us, const rtd_data_t& data) {
    if (!(xEventGroupGetBits(status_event_group) &  POWER_ON_BIT)) {
        ESP_LOGW(TAG, "In standby, not running.");
        gpio_set_level(GPIO_HBRIDGE_DIS, 1);
        return;
    }

    if (data.fault == Max31865Error::NoError) {
        // Good to go
        //uint64_t deltaT = time_us - s_last_time_us;

        if (!(xEventGroupGetBits(status_event_group) &  BOILER_LEVEL_OK_BIT)) {
            ESP_LOGE(TAG, "Not running, boiler level low");
            return;
        }

        ESP_LOGI(TAG, "BrewHead Temp=%f", data.temperature);

        s_last_time_us = time_us;
    } else {
        ESP_LOGE(TAG, "BrewHead sensor error %s", Max31865::errorToString(data.fault));
    }

}

void brew_temp_tec_hot_updated(uint64_t time_us, const rtd_data_t& data) {

}

void brew_temp_tec_cold_updated(uint64_t time_us, const rtd_data_t& data) {

}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        ESP_LOGI(TAG, "Powering down TEC");
        gpio_set_level(GPIO_HBRIDGE_DIS, 1);
    } else if (id == POWER_ACTIVE) {
        ESP_LOGI(TAG, "Resuming Boiler TEC");
        gpio_set_level(GPIO_HBRIDGE_DIS, 0);
    }
}


static void _init_h_bridge() {
    /*
 * Prepare and set configuration of timers
 * that will be used by LED Controller
 */
    ledc_timer_config_t ledc_timer;
    ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;          // timer mode
    ledc_timer.clk_cfg = LEDC_USE_APB_CLK;
    ledc_timer.duty_resolution = LEDC_TIMER_10_BIT; // resolution of PWM duty
    ledc_timer.timer_num = LEDC_TIMER_0;            // timer index
    ledc_timer.freq_hz = 20000;                      // frequency of PWM signal
    // Set configuration of timer0 for high speed channels
    ledc_timer_config(&ledc_timer);

    s_pwm_channel.channel = LEDC_CHANNEL_0;
    s_pwm_channel.duty = 0;
    s_pwm_channel.gpio_num = GPIO_HBRIDGE_PWM;
    s_pwm_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    s_pwm_channel.hpoint = 0;
    s_pwm_channel.timer_sel = LEDC_TIMER_0;
    ledc_channel_config(&s_pwm_channel);


    // Get our power events in place so we can run the process loop as needed
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));

    ESP_LOGI(TAG, "TEC initialised.");
}


void brew_temp_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;

    // Configure pins for H-Bridge

    // Output pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_HBRIDGE_DIR) |
            (1ULL << GPIO_HBRIDGE_DIS) |
            (1ULL << GPIO_HBRIDGE_PWM)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Input pins
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_HBRIDGE_SO)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    _init_h_bridge();

    // Enable H-Bridge
    gpio_set_level(GPIO_HBRIDGE_DIS, 1);
}
