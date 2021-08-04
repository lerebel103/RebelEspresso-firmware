#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <hal/gpio_types.h>
#include <hw_config.h>
#include <src/events.h>
#include <esp_event.h>
#include <esp_log.h>
#include "pump.h"
#include "out_signals.h"

#define TAG "pump"

static esp_event_loop_handle_t s_event_loop;
static bool _pump_sw_on = false;
static bool _boiler_refilling = false;
static bool s_descaled_entered = false;

static void _pump_on() {
    out_signals_set_level(OUT_SIGNALS_RELAY1, 1);
}

static void IRAM_ATTR _pump_off() {
    out_signals_set_level(OUT_SIGNALS_RELAY1, 0);
}

static void _refill_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == BOILER_REFILL_STARTED) {
        _boiler_refilling = true;
        ESP_LOGI(TAG, "Refill started, turning pump on");
        _pump_on();
    } else if (id == BOILER_REFILL_STOPPED || id == BOILER_REFILL_ERROR) {
        _boiler_refilling = false;
        if (!_pump_sw_on) {
            ESP_LOGI(TAG, "Refill stopped, turning off pump");
            _pump_off();
        }
    }
}

static void IRAM_ATTR _brew_switch_off(void *arg) {
    bool is_on = _pump_sw_on;

    _pump_sw_on = false;
    if (!_boiler_refilling) {
        _pump_off();
    }

    if (is_on) {
        // Only send end event if switch was previously on
        auto now_us = esp_timer_get_time();
        ESP_ERROR_CHECK(esp_event_post_to(s_event_loop, MACHINE_EVENTS, BREW_STOPPED, (void *) &now_us, 0,
                                          portMAX_DELAY));
    }
}

/*
 * It's annoying to have to do this.
 * Unfortunately we can't get the edge state accurately if positive and negative
 * interrupt is selected, which makes it unreliable to drive the pump from a single
 * configured ISR handler. So we need this secondary tick thing to ensure states
 * are consistent.
 */
static void _tick(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id != TICK) {
        return;
    }

    // Not running any of this in standby
    if (!(xEventGroupGetBits(status_event_group) & POWER_ON_BIT)) {
        return;
    }

    bool is_pump_powered = out_signals_get_level(OUT_SIGNALS_RELAY1);

    if (s_descaled_entered && gpio_get_level(PIN_IN_SYS_EN) == 1) {
        // Don't run normal pump on/off if we are in descale mode until the pump switch is cycled once.
        s_descaled_entered = false;
    } else if (!s_descaled_entered) {
        // maintain pump state with switch
        if (gpio_get_level(PIN_IN_SYS_EN) == 0 && !is_pump_powered) {
            _pump_sw_on = true;

            // Send start event then
            auto now_us = esp_timer_get_time();
            ESP_ERROR_CHECK(
                    esp_event_post_to(s_event_loop, MACHINE_EVENTS, BREW_STARTED, (void *) &now_us, sizeof(uint64_t),
                                      portMAX_DELAY));

            _pump_on();
        } else if (gpio_get_level(PIN_IN_SYS_EN) == 1 && is_pump_powered) {
            _brew_switch_off(nullptr);
        }
    } else {
        _pump_off();
    }
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        // Always stop pump regardless
        _pump_off();
        xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);
        s_descaled_entered = false;
    } else if (id == POWER_ACTIVE) {
        // If pump switch is on when active, we enter descaling mode
        if (gpio_get_level(PIN_IN_SYS_EN) == 0) {
            xEventGroupSetBits(status_event_group, DESCALE_MODE_BIT);
            s_descaled_entered = true;
        }
    }
}


void pump_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
    s_descaled_entered = false;

    // --- Configure input switch that drives the pump
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << PIN_IN_SYS_EN)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Get eveything synced up
    _tick(NULL, MACHINE_EVENTS, TICK, NULL);

    // Now install switch interrupt and event handlers for boiler refill events
    gpio_isr_handler_add(PIN_IN_SYS_EN, _brew_switch_off, NULL);

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_STARTED,
                                                    _refill_events, s_event_loop));

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_STOPPED,
                                                    _refill_events, s_event_loop));

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_ERROR,
                                                    _refill_events, s_event_loop));

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, TICK,
                                                    _tick, s_event_loop));

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));
}

