#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <sys/nvram_store.h>
#include <cJson.h>
#include <esp_log.h>
#include <events.h>
#include <esp32/pm.h>
#include <sys/homekit.h>
#include <sys/ota.h>
#include <sys/wifi_connect.h>
#include <hal/timer_types.h>
#include <driver/timer.h>
#include <driver/gpio.h>
#include <hw/rtds.h>

#include "controller.h"
#include "state.h"
#include "sys/mqtt.h"

#define TIMER_DIVIDER         16  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds

#define TIMER_INTERVAL0_SEC   ( 1.0 )

#define CONTROL_LOOP_PERIOD 1000
#define IOT_SEND_INTERVAL 5000

#define KEY_ENABLED "ctrl_enabled"

static const char *TAG = "controller";

static rtds_cfg_t s_rtds_cfg;

TickType_t g_last_iot_send = 0;
static controller_cfg_t g_controller_cfg;

static esp_event_loop_handle_t s_event_loop;
static TickType_t s_last_status_update_tick = 0;
static bool s_ota_needed = true;

void IRAM_ATTR _process_loop_isr(void *para) {
    // Re-enable interrupt, safe to do so
    TIMERG0.int_clr_timers.t0 = 1;
    TIMERG0.hw_timer[0].config.alarm_en = TIMER_ALARM_EN;

    // Ok read all sensors
    rtd_data_t data;
    rtds_read_1(&s_rtds_cfg, &data);
}

static void _init_hw_timer() {
    ESP_LOGI(TAG, "Installing ISR service");
    gpio_install_isr_service(
            ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_LEVEL3);

    static timer_idx_t timer_idx = TIMER_0;

    /* Select and initialize basic parameters of the timer */
    timer_config_t config = {
            .alarm_en = TIMER_ALARM_EN,
            .counter_en = TIMER_START,
            .intr_type = TIMER_INTR_LEVEL,
            .counter_dir = TIMER_COUNT_UP,
            .auto_reload = TIMER_AUTORELOAD_EN,
            .divider = TIMER_DIVIDER,
    }; // default clock source is APB

    ESP_LOGI(TAG, "Configuring timer");

    ESP_ERROR_CHECK(timer_init(TIMER_GROUP_0, timer_idx, &config));

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    ESP_ERROR_CHECK(timer_set_counter_value(TIMER_GROUP_0, timer_idx, 0x00000000ULL));

    /* Configure the alarm value and the interrupt on alarm. */
    ESP_ERROR_CHECK(timer_set_alarm_value(TIMER_GROUP_0, timer_idx, (TIMER_INTERVAL0_SEC) * TIMER_SCALE));
    ESP_ERROR_CHECK(timer_isr_register(TIMER_GROUP_0, timer_idx, _process_loop_isr,
                                       nullptr, ESP_INTR_FLAG_LEVEL3, NULL));
    ESP_ERROR_CHECK(timer_enable_intr(TIMER_GROUP_0, timer_idx));
    ESP_ERROR_CHECK(timer_start(TIMER_GROUP_0, timer_idx));
}

void controller_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
    nvram_store_read_u8(KEY_ENABLED, (uint8_t *) &g_controller_cfg.enabled, g_controller_cfg.enabled);

    _init_hw_timer();

    rtds_init(&s_rtds_cfg);

    // Causes initial state to be sent
    xEventGroupSetBits(status_event_group, SEND_STATE_BIT);
}


static void send_iot_events(TickType_t tick) {
    if (tick >= (g_last_iot_send + IOT_SEND_INTERVAL)) {

        g_last_iot_send = tick;
    }
}

void controller_enter_loop() {
    bool go = true;


    while (go) {
        time_t time_millis = xTaskGetTickCount() * portTICK_PERIOD_MS;

        wifi_tick(time_millis);
        homekit_tick(time_millis);

        // Always trigger display refresh at the back of new temperatures
        xEventGroupSetBits(status_event_group, REFRESH_DISPLAY_BIT);


        // Send MQTT stuff as required
        if (xEventGroupGetBits(status_event_group) & MQTT_CONNECTED_BIT) {
            auto send_state = xEventGroupGetBits(status_event_group) & SEND_STATE_BIT;
            if (send_state && (time_millis - s_last_status_update_tick) > 10000) {
                // No earlier than 10s for Google IoT
                ESP_LOGI(TAG, "Sending new state");
                s_last_status_update_tick = time_millis;
                state_send(time(NULL));
                xEventGroupClearBits(status_event_group, SEND_STATE_BIT);
            }
            send_iot_events(time_millis);

            // Do we need to run OTA (wait 15 seconds after we connect to let things settle first)?
            if (s_ota_needed && ota_is_configured() && (time_millis - mqtt_last_connect_attempt()) > 15000) {
                ota_run();
                s_ota_needed = false;
            }
        } else {
            // This will trigger another ota check again if MQTT drops out, bit of a hack really
            s_ota_needed = true;
        }


        state_print_memory_info();

        // Approximately every second...
        time_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (CONTROL_LOOP_PERIOD > (now - time_millis)) {
            // Run event loop dispatch
            vTaskDelay((CONTROL_LOOP_PERIOD - (now - time_millis)) / portTICK_PERIOD_MS);
        }
    }
}

const bool &controller_is_enabled() {
    return g_controller_cfg.enabled;
}

void controller_enable(bool enabled) {
    g_controller_cfg.enabled = enabled;
    nvram_store_write_u8(KEY_ENABLED, g_controller_cfg.enabled);
    // Trigger status send
    xEventGroupSetBits(status_event_group, SEND_STATE_BIT);
    ESP_LOGI(TAG, "Enable set to %d", enabled);
}

// ---------------------------------------------------------------------------------------------------------------------
// Config stuff
// ---------------------------------------------------------------------------------------------------------------------

void controller_cfg_to_json(cJSON *root) {
    cJSON_AddBoolToObject(root, "enabled", g_controller_cfg.enabled);
    // Not quite the right place for this... :-(

    // Trigger status send
    xEventGroupSetBits(status_event_group, SEND_STATE_BIT);
}


void controller_cfg_from_json(const cJSON *config) {
    ESP_LOGD(TAG, "Got remote config");
    {
        cJSON *item = cJSON_GetObjectItem(config, "enabled");
        if (cJSON_IsBool(item)) {
            bool enabled = item->valueint;
            if (enabled != g_controller_cfg.enabled) {
                controller_enable(enabled);
            }
        }
    }
}
