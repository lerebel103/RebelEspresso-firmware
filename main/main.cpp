
extern "C" {
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
}

#include <sys/ota.h>
#include <esp_event.h>
#include <Max31865.h>
#include <hal/ledc_types.h>
#include <driver/ledc.h>

#include "hw_config.h"
#include "state.h"
#include "control/controller.h"
#include "thing_info.h"
#include "sys/nvram_store.h"
#include "sys/wifi_connect.h"
#include "sys/sntp.h"
#include "sys/mqtt.h"
#include "sys/homekit.h"

#include "hw/oled/display.h"

#define TAG  "main"

// Event group pointer so we get system events to sync up
EventGroupHandle_t status_event_group;


void
_read_temperature(Max31865 &tempSensor, const max31865_rtd_config_t &rtdConfig, const max31865_config_t &readConfig, int idx) {
    uint16_t rtd;
    Max31865Error fault = Max31865Error::NoError;

    tempSensor.clearFault();
    tempSensor.getRTD(&rtd, &fault);

    float Rrtd = (rtd * rtdConfig.ref) / (1U << 15U);
    //float temp = Max31865::RTDtoTemperature(rtd, rtdConfig);
    ESP_LOGI("Temperature", "  [%d] -> R=%f, fault: %d", idx, Rrtd, (int) fault);
}

static ledc_channel_config_t g_ledc_channel;
static uint32_t rpm_counter = 0;
static TickType_t last_rpm_calc_time = 0;
static TickType_t last_rpm_heart_beat = 0;
static uint16_t g_rpm = 0;
static uint8_t g_duty = 0;
static bool s_enabled = false;

static void _config_hbridge() {

    /*
     * Prepare and set configuration of timers
     * that will be used by LED Controller
     */
    ledc_timer_config_t ledc_timer;
    ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;          // timer mode
    ledc_timer.clk_cfg = LEDC_USE_APB_CLK;
    ledc_timer.duty_resolution = LEDC_TIMER_10_BIT; // resolution of PWM duty
    ledc_timer.timer_num = LEDC_TIMER_0;            // timer index
    ledc_timer.freq_hz = 15000;                      // frequency of PWM signal
    // Set configuration of timer0 for high speed channels
    ledc_timer_config(&ledc_timer);

    g_ledc_channel.channel = LEDC_CHANNEL_0;
    g_ledc_channel.duty = 0;
    g_ledc_channel.gpio_num = GPIO_HBRIDGE_PWM;
    g_ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    g_ledc_channel.hpoint = 0;
    g_ledc_channel.timer_sel = LEDC_TIMER_0;
    ledc_channel_config(&g_ledc_channel);

    // Turn off


    ESP_LOGI(TAG, "Fan initialised.");
}

#define ESP_INTR_FLAG_DEFAULT 0

static void IRAM_ATTR gpio_isr_handler1(void* arg) {
    bool trigger = true;
    if (gpio_get_level(GPIO_SW1)) {
        trigger = false;
    }
    gpio_set_level(GPIO_TRIG2, trigger);
}

static void IRAM_ATTR gpio_isr_handler2(void* arg) {
    bool trigger = true;
    if (gpio_get_level(GPIO_SW2)) {
        trigger = false;
    }
    gpio_set_level(GPIO_TRIG2, trigger);
}

static void IRAM_ATTR gpio_isr_handler3(void* arg) {
    bool trigger = true;
    if (gpio_get_level(GPIO_SW3)) {
        trigger = false;
    }
    gpio_set_level(GPIO_TRIG3, trigger);
}


void switches_init() {

    // Output pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
                    (1ULL << GPIO_SW1) |
                    (1ULL << GPIO_SW3)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_SW1, gpio_isr_handler1, NULL);
    gpio_isr_handler_add(GPIO_SW3, gpio_isr_handler3, NULL);
    // gpio_isr_handler_add(GPIO_SW3, gpio_isr_handler3, NULL);
}

int btn_home = 0;
int btn_up = 0;
int btn_down = 0;

static void IRAM_ATTR gpio_isr_btn_home(void* arg) {
    btn_home++;
}

static void IRAM_ATTR gpio_isr_btn_up(void* arg) {
    btn_up++;
}

static void IRAM_ATTR gpio_isr_btn_down(void* arg) {
    btn_down++;
}

void buttons_init() {

    // Output pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_BTN_HOME) |
            (1ULL << GPIO_BTN_UP) |
            (1ULL << GPIO_BTN_DOWN)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_BTN_UP, gpio_isr_btn_up, NULL);
    gpio_isr_handler_add(GPIO_BTN_DOWN, gpio_isr_btn_down, NULL);
    gpio_isr_handler_add(GPIO_BTN_HOME, gpio_isr_btn_home, NULL);
}

void water_level_sensor_init() {

    // Output pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_SEN_W_LEVEL)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Now for powering the probe
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_SEN_PW)
    );
    gpio_config(&io_conf);

    gpio_set_level(GPIO_SEN_PW, 0);
}


void hbridge_handle(int duty) {
    gpio_set_level(GPIO_HBRIDGE_DIS, 1);

/*    if (gpio_get_level(GPIO_HBRIDGE_SO)) {
        gpio_set_level(GPIO_HBRIDGE_DIS, 1);
        vTaskDelay(pdMS_TO_TICKS(100));

        ESP_LOGW("Hbridge", "Got error from hbridge, turning off and on");

        gpio_set_level(GPIO_HBRIDGE_DIS, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ledc_set_duty(g_ledc_channel.speed_mode, g_ledc_channel.channel, (uint32_t) (1024 * duty / 100.0f));
    ledc_update_duty(g_ledc_channel.speed_mode, g_ledc_channel.channel);
    ESP_LOGI("Hbridge", "Duty set to %d", duty);
    */
}

extern "C" void app_main() {
    esp_event_loop_args_t event_loop_args = {
            .queue_size = 5,
            .task_name = "App Event Loop", // No task will be created
            .task_priority = uxTaskPriorityGet(NULL),
            .task_stack_size = 2548,
            .task_core_id = tskNO_AFFINITY
    };
    esp_event_loop_handle_t event_loop;
    ESP_ERROR_CHECK(esp_event_loop_create(&event_loop_args, &event_loop));
    status_event_group = xEventGroupCreate();

    esp_log_level_set("gpio", ESP_LOG_ERROR);

    // Do core initialisations first
    nvram_store_init();
    store_inc_cycle_count(); // Record number of power cycles.

    thing_info_init();
    state_print_system_info();
    controller_init(event_loop);
    //display_init();

    // Now for witi, ota, mqtt
    wifi_init();
    wifi_set_ssid("ortyma");
    wifi_set_password("pho3nixlerebel103");

    ota_init(thing_info_id(), THING_TYPE, FIRMWARE_VERSION, HARDWARE_REVISION);


    mqtt_set_client_private_key(    "-----BEGIN EC PRIVATE KEY-----\n"
                                    "MHcCAQEEIDvKD7cTp5i6OeJhXvw/PxQFWs0rq5wAt3hTOUScpJr1oAoGCCqGSM49\n"
                                    "AwEHoUQDQgAE3a5tg30Yse9WDVIzNYI5p9AXB9ipSBMLg1/yv6fweoNikB+/mbtg\n"
                                    "55cJUWmK2ZbxLvlwh19Exe4DVZNfZVL6og==\n"
                                    "-----END EC PRIVATE KEY-----"
    );

    mqtt_set_registry_id("RebelEspresso");
    mqtt_set_location("asia-east1");
    mqtt_set_project_id("rebel-espresso");

    mqtt_set_ota_cfg_cb(ota_cfg_from_json);
    mqtt_set_controller_cfg_cb(controller_cfg_from_json);


    mqtt_init();

    homekit_init();


    switches_init();
    buttons_init();
    water_level_sensor_init();

    auto tempSensor = Max31865(GPIO_MISO, GPIO_MOSI, GPIO_SCK, GPIO_RTD_CS);
    max31865_config_t tempConfig = {};
    tempConfig.autoConversion = false;
    tempConfig.faultDetection = Max31865FaultDetection::AutoDelay;
    tempConfig.vbias = true;
    tempConfig.filter = Max31865Filter::Hz50;
    tempConfig.nWires = Max31865NWires::Two;
    max31865_rtd_config_t rtdConfig = {};
    rtdConfig.nominal = 1000.0f;
    rtdConfig.ref = 4000.0f;
    ESP_ERROR_CHECK(tempSensor.begin(tempConfig));
    //ESP_ERROR_CHECK(tempSensor.setRTDThresholds(0x2000, 0x2500));
    //ESP_ERROR_CHECK(tempSensor.setRTDThresholds(0x0000, 0xFFFF));
    //ESP_ERROR_CHECK(tempSensor.setRTDThresholds(0x0000, 0x6500));


    max31865_config_t readConfig = {};
    tempSensor.getConfig(&readConfig);

    // Output pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
                    (1ULL << GPIO_RTD_A0) |
                    (1ULL << GPIO_RTD_A1) |
                    (1ULL << GPIO_TRIG1) |
                    (1ULL << GPIO_TRIG2) |
                    (1ULL << GPIO_TRIG3) |
                    (1ULL << GPIO_TRIG4) |
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

    // Enable H-Bridge
    gpio_set_level(GPIO_HBRIDGE_DIS, 1);

    _config_hbridge();

    gpio_set_level(GPIO_HBRIDGE_DIR, 1);
    int duty = 25;

    int count = 0;
    while (true) {
        gpio_set_level(GPIO_RTD_A0, 0);
        gpio_set_level(GPIO_RTD_A1, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        _read_temperature(tempSensor, rtdConfig, readConfig, 1);

        gpio_set_level(GPIO_RTD_A0, 1);
        gpio_set_level(GPIO_RTD_A1, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        _read_temperature(tempSensor, rtdConfig, readConfig,2 );

        gpio_set_level(GPIO_RTD_A0, 0);
        gpio_set_level(GPIO_RTD_A1, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        _read_temperature(tempSensor, rtdConfig, readConfig, 3);

        gpio_set_level(GPIO_RTD_A0, 1);
        gpio_set_level(GPIO_RTD_A1, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        _read_temperature(tempSensor, rtdConfig, readConfig, 4);


        ESP_LOGI("Temperature", "\n");

        count ++;



        if (count %2) {
            gpio_set_level(GPIO_SEN_PW, 0);

            //gpio_set_level(GPIO_TRIG1, 0);
            //gpio_set_level(GPIO_TRIG2, 0);
            //gpio_set_level(GPIO_TRIG3, 0);
            //gpio_set_level(GPIO_TRIG4, 0);

        } else {
            gpio_set_level(GPIO_SEN_PW, 1);

            //gpio_set_level(GPIO_TRIG1, 1);
            //vTaskDelay(pdMS_TO_TICKS(100));
            //gpio_set_level(GPIO_TRIG2, 1);
            //vTaskDelay(pdMS_TO_TICKS(100));

            //gpio_set_level(GPIO_TRIG3, 1);
            //vTaskDelay(pdMS_TO_TICKS(100));
            //gpio_set_level(GPIO_TRIG4, 1);

        }

        hbridge_handle(duty);

        ESP_LOGI("Buttons", "HOME=%d, UP=%d, DOWN=%d", btn_home, btn_up, btn_down);

        vTaskDelay(pdMS_TO_TICKS(500));

    }



    // Here's our control loop
    controller_enter_loop();
    esp_event_loop_delete(event_loop);
}



