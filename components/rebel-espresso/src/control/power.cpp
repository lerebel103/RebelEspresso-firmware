#include "power.h"

#include <src/events.h>
#include <hal/gpio_types.h>
#include <hw_config.h>
#include <esp_event.h>
#include <esp_log.h>

const static char* TAG = "power";

static esp_event_loop_handle_t s_event_loop;

static void IRAM_ATTR _standby () {
    if ((xEventGroupGetBitsFromISR(status_event_group) & POWER_ON_BIT)) {
        xEventGroupClearBitsFromISR(status_event_group, POWER_ON_BIT);
        ESP_ERROR_CHECK(esp_event_isr_post_to(s_event_loop, MACHINE_EVENTS, POWER_STANDBY, nullptr, 0, nullptr));
    }
}

static void _active() {
    if (!(xEventGroupGetBits(status_event_group) & POWER_ON_BIT)) {
        ESP_LOGI(TAG, "Entering ACTIVE state.");
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        ESP_ERROR_CHECK(esp_event_isr_post_to(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE, nullptr, 0, nullptr));
    }
}

static void IRAM_ATTR _power_off(void *arg) {
    _standby();
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

    // maintain pump state with switch
    if(gpio_get_level(GPIO_SW3) == 0) {
        _active();
    } else {
        _standby();
    }
}

void power_standby() {
    _standby();
}

void power_active() {
    _active();
}


void power_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
    
    // No power until proven otherwise
    xEventGroupClearBits(status_event_group, POWER_ON_BIT);
    ESP_ERROR_CHECK(esp_event_isr_post_to(s_event_loop, MACHINE_EVENTS, POWER_STANDBY, nullptr, 0, nullptr));

    // --- Configure input switch that drives power state
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << GPIO_SW3)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Now install switch interrupt and event handlers for boiler refill events
    gpio_isr_handler_add(GPIO_SW3, _power_off, NULL);

    // We want tick events
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, TICK,
                                                    _tick, s_event_loop));



}
