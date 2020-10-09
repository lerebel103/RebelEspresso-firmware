#include <FreeRTOS.h>
#include <task.h>

#include <hal/gpio_types.h>
#include <hw_config.h>
#include <src/events.h>
#include <esp_event.h>
#include <esp_log.h>
#include "pump.h"

#define TAG "pump"

static esp_event_loop_handle_t s_event_loop;
static bool _pump_sw_on = false;
static bool _boiler_refilling = false;

static void IRAM_ATTR _pump_on() {
    gpio_set_level(GPIO_TRIG2_REL1, 1);

}

static void IRAM_ATTR _pump_off() {
    gpio_set_level(GPIO_TRIG2_REL1, 0);

}

static void _refill_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == BOILER_REFILL_STARTED) {
        _boiler_refilling = true;
        ESP_LOGI(TAG, "Refill started, turning pump on");
        _pump_on();
    } else if (id == BOILER_REFILL_STOPPED) {
        _boiler_refilling = false;
        if (!_pump_sw_on) {
            ESP_LOGI(TAG, "Refill stopped, turning off pump");
            _pump_off();
        }
    }
}

static void IRAM_ATTR _brew_switch_off(void *arg) {
    _pump_sw_on = false;
    if (!_boiler_refilling) {
        _pump_off();
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
    if (!(xEventGroupGetBits(status_event_group) &  POWER_ON_BIT)) {
        return;
    }

    // maintain pump state with switch
    if(gpio_get_level(GPIO_SW1) == 0) {
        _pump_sw_on = true;
        _pump_on();
    } else {
        _brew_switch_off(nullptr);
    }
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        // Always stop pump regardless
        _pump_off();
    } else if (id == POWER_ACTIVE) {
    }
}


void pump_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;

    // --- Output pins
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_TRIG2_REL1)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // --- Configure input switch that drives the pump
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_SW1)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Get eveything synced up
    _tick(NULL, MACHINE_EVENTS, TICK, NULL);

    // Now install switch interrupt and event handlers for boiler refill events
    gpio_isr_handler_add(GPIO_SW1, _brew_switch_off, NULL);

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_STARTED,
                                                    _refill_events, s_event_loop));

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_STOPPED,
                                                    _refill_events, s_event_loop));

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, TICK,
                                                    _tick, s_event_loop));

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));
}

