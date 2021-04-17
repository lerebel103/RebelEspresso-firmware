#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


#include <signal.h>
#include <esp_event.h>
#include <src/events.h>

static bool requestedFactoryReset = false;
static bool clearPairings = false;
static esp_event_loop_handle_t s_event_loop;
static bool s_init = false;



void _send_power_state(void *_Nullable context, size_t contextSize) {
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {

    } else if (id == POWER_ACTIVE) {

    }
}

void homekit_terminate() {
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY, _power_events));
    ESP_ERROR_CHECK(esp_event_handler_unregister_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE, _power_events));


    // Wait for HAP to terminate - gah this is bad coding indeed
    while(s_init) {
        vTaskDelay(10);
    }
}

void homekit_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;

    // Register power events so we can send to home kit
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));

    s_init = true;
}

bool homekit_is_initialised() {
    return s_init;
}
