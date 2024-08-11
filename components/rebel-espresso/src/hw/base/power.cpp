#include "power.h"

#include <src/events.h>
#include <hal/gpio_types.h>
#include <hw_config.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp32/pm.h>
#include <esp_pm.h>
#include <driver/gpio.h>

const static char *TAG = "power";


//static bool s_is_low_power = false;
static bool s_active_toggled = false;

static void _standby() {
    if ((xEventGroupGetBits(status_event_group) & POWER_ON_BIT)) {
        xEventGroupClearBits(status_event_group, POWER_ON_BIT);
        ESP_ERROR_CHECK(esp_event_post( MACHINE_EVENTS, POWER_STANDBY, nullptr, 0, portMAX_DELAY));
    }
}


static void _active() {
    if (!(xEventGroupGetBits(status_event_group) & POWER_ON_BIT)) {
        ESP_LOGI(TAG, "Entering ACTIVE state.");
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        ESP_ERROR_CHECK(esp_event_post( MACHINE_EVENTS, POWER_ACTIVE, nullptr, 0, portMAX_DELAY));
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

    if (gpio_get_level(PIN_IN_SYS_EN) == 0) {
        /*if (s_is_low_power) {
            // Enter normal power mode
            esp_pm_config_esp32_t pm_config = {
                    .max_freq_mhz = 240,
                    .min_freq_mhz = 240,
                    .light_sleep_enable = false
            };

            ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
            s_is_low_power = false;
        }*/

        _active();

        // Always reset active toggle if we are forced on
        s_active_toggled = false;
    } else if (!s_active_toggled){
        _standby();

        /*if (!s_is_low_power) {
            // Enter lower power mode
            esp_pm_config_esp32_t pm_config = {
                    .max_freq_mhz = 160,
                    .min_freq_mhz = 160,
                    .light_sleep_enable = false
            };

            // Adjust Dynamic Frequency Scaling range and enter light sleep
            ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
            s_is_low_power = true;
        }*/
    }
}

extern "C"
void power_standby() {
    s_active_toggled = false;
    if (gpio_get_level(PIN_IN_SYS_EN) != 0) {
        _standby();
    }
}

extern "C"
void power_active() {
    s_active_toggled = true;
    _active();
}

extern "C"
bool power_is_active() {
    bool is_on = xEventGroupGetBits(status_event_group) & POWER_ON_BIT;
    return is_on;
}

void power_init() {


    // No power until proven otherwise
    xEventGroupClearBits(status_event_group, POWER_ON_BIT);
    ESP_ERROR_CHECK(esp_event_post( MACHINE_EVENTS, POWER_STANDBY, nullptr, 0, portMAX_DELAY));

    // --- Configure input switch that drives power state
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = ((1ULL << PIN_IN_SYS_EN));
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // We want tick events
    ESP_ERROR_CHECK(esp_event_handler_register( MACHINE_EVENTS, TICK, _tick, nullptr));
}
