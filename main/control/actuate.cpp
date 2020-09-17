#include "actuate.h"

#include <freertos/task.h>
#include <freertos/task.h>
#include <hw_config.h>
#include <hal/gpio_types.h>
#include <driver/gpio.h>
#include <cmath>
#include <perfmon.h>

#include <driver/adc.h>
#include <math.h>
#include <esp_adc_cal.h>
#include <driver/dac.h>


#define DEFAULT_VREF    1100
#define NO_OF_SAMPLES   24

#define BLINKING_TIME 1000
#define SWITCH_BLIP_TIME 300
#define OBSTRUCTION_DETECT_TIME_MS 20000

const static char* TAG = "actuate";

static bool _go = false;

static const adc_unit_t unit = ADC_UNIT_1;
static esp_adc_cal_characteristics_t *adc_chars;
static TickType_t s_closing_started_time = 0;

static Actuate_State_t s_actuate_state = ACTUATE_STATE_CLOSED;

enum led_state_t {
    LED_OFF,
    LED_BLINKING,
    LED_ON
};

static led_state_t s_green_state;
static led_state_t s_red_state;



led_state_t compute_state(
        TickType_t& last_on_time,
        int& last_level,
        TickType_t now, int level) {

    led_state_t state;

    if (now - last_on_time < BLINKING_TIME) {
        state = LED_BLINKING;
    } else if (level) {
        state = LED_ON;
    } else {
        state = LED_OFF;
    }

    if (level && last_level != level) {
        last_on_time = now;
    }
    last_level = level;

    return state;
}

bool _get_level(adc1_channel_t channel) {
    // Read voltage from ADC
    double v_in = 0;
    for (int i=0; i<NO_OF_SAMPLES; i++) {
        int raw = adc1_get_raw(channel);
        v_in += esp_adc_cal_raw_to_voltage(raw, adc_chars);
    }
    v_in = (v_in / NO_OF_SAMPLES) / 1000;
    
    ESP_LOGD(TAG, "Pin %d, Voltage: %f", channel, v_in);
    return v_in > 2.8;
}

void _do_led_states(void* arg) {
    TickType_t last_green_on_time = 0;
    int last_green_level = 0;
    TickType_t last_red_on_time = 0;
    int last_red_level = 0;

    while(_go) {
        auto now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        auto green_level = _get_level(GREEN_LED_INPUT_PIN);
        s_green_state = compute_state(last_green_on_time, last_green_level, now, green_level);
        ESP_LOGD(TAG, "----> Green: %d", s_green_state);

        auto red_level = _get_level(RED_LED_INPUT_PIN);
        s_red_state = compute_state(last_red_on_time, last_red_level, now, red_level);
        ESP_LOGD(TAG, "----> Red: %d", s_red_state);

        vTaskDelay( 10 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

void actuate_init() {
    _go = true;

    // Configure ADC
    auto attenuation = ADC_ATTEN_DB_11;
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(GREEN_LED_INPUT_PIN, attenuation);

    ESP_LOGI(TAG, "Getting calibration");
    //Characterize ADC
    adc_chars = static_cast<esp_adc_cal_characteristics_t *>(calloc(1, sizeof(esp_adc_cal_characteristics_t)));
    esp_adc_cal_characterize(unit, attenuation, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);

    // Output pin for open/stop/close
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << SWITCH_OUTPUT_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(SWITCH_OUTPUT_PIN, 0);

    xTaskCreate(_do_led_states, "led states", 2048, NULL, 5, NULL);
}

Actuate_State_t actuate_get_state() {
    return s_actuate_state;
}

void actuate_blip_switch() {
    // Momentary push switch
    gpio_set_level(SWITCH_OUTPUT_PIN, 1);
    vTaskDelay(SWITCH_BLIP_TIME / portTICK_PERIOD_MS);
    gpio_set_level(SWITCH_OUTPUT_PIN, 0);
}


void actuate_tick(TickType_t now) {


    // Work out state then
    if (s_green_state == LED_OFF && s_red_state == LED_ON) {
        s_actuate_state = ACTUATE_STATE_CLOSED;
        s_closing_started_time = 0;
    } else if (s_green_state == LED_ON && s_red_state == LED_OFF) {
        s_actuate_state = ACTUATE_STATE_OPENED;
        s_closing_started_time = 0;
    } else if (s_green_state == LED_BLINKING && s_red_state == LED_OFF) {
        s_actuate_state = ACTUATE_STATE_OPENING;
        s_closing_started_time = 0;
    } else if (s_green_state == LED_OFF && s_red_state == LED_BLINKING) {
        s_actuate_state = ACTUATE_STATE_CLOSING;
        if (s_closing_started_time <= 0) {
            s_closing_started_time = now;
        }
    } else if (s_green_state == LED_BLINKING && s_red_state == LED_BLINKING) {
        s_actuate_state = ACTUATE_STATE_STOPPED;
        s_closing_started_time = 0;
    } else {
        s_actuate_state = ACTUATE_STATE_ERROR;
        s_closing_started_time = 0;
    }

    // Now if closed is flashing for over a period of time, we have obstruction
    if (s_closing_started_time != 0 && (now - s_closing_started_time) > OBSTRUCTION_DETECT_TIME_MS) {
        s_actuate_state = ACTUATE_STATE_OBSTRUCTION;
    }

}