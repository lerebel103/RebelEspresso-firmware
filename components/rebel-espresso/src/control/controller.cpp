#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <sys/nvram_store.h>
#include <esp_log.h>
#include <events.h>
#include <sys/ota.h>
#include <hal/timer_types.h>
#include <driver/timer.h>
#include <driver/gpio.h>
#include <hw/rtds.h>
#include <esp_event.h>

#include "controller.h"
#include "process_loop.h"
#include "boiler_refill.h"
#include "boiler_temp.h"
#include "brew_temp.h"
#include "pump.h"
#include "power.h"
#include "setpoint_selector.h"
#include "iot.h"


#define KEY_ENABLED "ctrl_enabled"

static const char *TAG = "controller";

static rtds_cfg_t s_rtds_cfg;

static controller_cfg_t g_controller_cfg;

static esp_event_loop_handle_t s_event_loop;

static bool _go = true;

/* Event source task related definitions */
ESP_EVENT_DEFINE_BASE(MACHINE_EVENTS);

#define ESP_INTR_FLAG_DEFAULT \
    (ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_LEVEL2 |ESP_INTR_FLAG_LEVEL3)


void controller_init(esp_event_loop_handle_t event_loop) {
    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    s_event_loop = event_loop;
    nvram_store_read_u8(KEY_ENABLED, (uint8_t *) &g_controller_cfg.enabled, g_controller_cfg.enabled);

    boiler_refill_init(event_loop);
    boiler_temp_init(event_loop);
    pump_init(event_loop);
    brew_temp_init(event_loop);
    setpoint_selector_init(event_loop);
    rtds_init(&s_rtds_cfg);
    process_loop_init(event_loop);
    power_init(event_loop);
    iot_init(event_loop);

    // Causes initial state to be sent
    xEventGroupSetBits(status_event_group, SEND_STATE_BIT);
}

void controller_enter_loop() {
    static auto loop_interval_us = 100e3;
    while (_go) {
        // Keeps going regardless of power state, emit event forever
        auto now_us = esp_timer_get_time();
        ESP_ERROR_CHECK(esp_event_post_to(s_event_loop, MACHINE_EVENTS, TICK, (void*)&now_us, sizeof(uint64_t), portMAX_DELAY));
        auto after_us = esp_timer_get_time();

        auto delay_ms = (loop_interval_us - (double)(after_us - now_us)) / 1e3;
        if (delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }

    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------------------------------------------------
// Config stuff
// ---------------------------------------------------------------------------------------------------------------------

void controller_cfg_to_json(cJSON *root, const char* base_key) {
    auto boiler_cfg = boiler_temp_get_cfg();
    boiler_cfg.to_json(root, base_key);

    auto brew_cfg = brew_temp_get_cfg();
    brew_cfg.to_json(root, base_key);

    auto boiler_refill_cfg = boiler_refill_get_cfg();
    boiler_refill_cfg.to_json(root, base_key);

    ota_cfg_to_json(root, base_key);
}

void controller_status_to_json(cJSON *root, const char* base_key) {
    auto boiler_status = boiler_temp_get_status();
    boiler_status.to_json(root, base_key);

    auto brew_status = brew_temp_get_status();
    brew_status.to_json(root, base_key);

    auto refill_status = boiler_refill_get_status();
    refill_status.to_json(root, base_key);
}

void controller_handle_new_cfg(const cJSON* cfg) {
    char* json = cJSON_Print(cfg);
    ESP_LOGI(TAG, "Got remote config %s", json);
    cJSON_free(json);

    // Pass down to each component, they will deal with it - it's a bit lazy really
    boiler_temp_update_cfg(cfg);
    brew_temp_update_cfg(cfg);
    boiler_refill_update_cfg(cfg);
    ota_update_cfg(cfg);

    // Trigger status send
    xEventGroupSetBits(status_event_group, SEND_STATE_BIT);
}

