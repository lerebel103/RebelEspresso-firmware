#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <hal/gpio_types.h>
#include <hw_config.h>
#include <src/events.h>
#include <esp_event.h>
#include <esp_log.h>
#include "setpoint_selector.h"
#include "boiler_temp.h"

#define TAG "setpoint_selector"

static esp_event_loop_handle_t s_event_loop;
static bool _setpoint_selector_sw_on = false;
static bool _boiler_refilling = false;

static void IRAM_ATTR _setpoint_selector_on() {
    boiler_set_active_setpoint(1);
}

static void IRAM_ATTR _setpoint_selector_off() {
    boiler_set_active_setpoint(0);
}

static void IRAM_ATTR _selector_switch_off(void *arg) {
    _setpoint_selector_sw_on = false;
    if (!_boiler_refilling) {
        _setpoint_selector_off();
    }
}

/*
 * It's annoying to have to do this.
 * Unfortunately we can't get the edge state accurately if positive and negative
 * interrupt is selected, which makes it unreliable to drive the setpoint_selector from a single
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

    // maintain setpoint_selector state with switch
    if(gpio_get_level(GPIO_SW2) == 0) {
        ESP_LOGE(TAG, "steam ON");
        _setpoint_selector_sw_on = true;
        _setpoint_selector_on();
    } else {
        ESP_LOGE(TAG, "steam OFF");
        _selector_switch_off(nullptr);
    }
}

void setpoint_selector_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;

    gpio_config_t io_conf;

    // --- Configure input switch that drives the setpoint_selector
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_SW2)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Get eveything synced up
    _tick(NULL, MACHINE_EVENTS, TICK, NULL);

    // Now install switch interrupt and event handlers for boiler refill events
    gpio_isr_handler_add(GPIO_SW2, _selector_switch_off, NULL);

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, TICK,
                                                    _tick, s_event_loop));

}

