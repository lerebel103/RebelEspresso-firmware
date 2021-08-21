#include <hw_config.h>
#include <esp_event.h>
#include "boiler_refill_states.h"
#include "state_machine.h"
#include "events.h"
#include "out_signals.h"

const static char *TAG = "refill";


static StateCtx_t<RefillState_t> s_state;
static esp_event_loop_handle_t s_event_loop;
static bool s_current_level_ok = false;
static bool s_in_error = false;
static const boiler_refill_cfg_t *s_cfg = nullptr;
static TickType_t s_level_stable_ms = 0;


static void _start_refill() {
    // Open solenoid valve
    ESP_LOGI(TAG, "Opening refill solenoid");
    out_signals_set_level(OUT_SIGNALS_RELAY2, 1);

    ESP_ERROR_CHECK(esp_event_post_to(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_STARTED, nullptr, 0,
                                      portMAX_DELAY));
}

static void _stop_refill() {
    // Turn pump off and close solenoid valve
    ESP_ERROR_CHECK(esp_event_post_to(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_STOPPED, nullptr, 0,
                                      portMAX_DELAY));

    ESP_LOGI(TAG, "Closing refill solenoid");
    out_signals_set_level(OUT_SIGNALS_RELAY2, 0);
}

static void _state_unknown_enter(uint64_t timestamp) {
    xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);
}

static void _state_unknown_process(uint64_t timestamp) {
    // Don't know yet where we are, evaluate initial level reading
    if (s_cfg->start_delay_ms == 0) {
        if (s_current_level_ok) {
            state_machine_transition(s_state, timestamp, REFILL_STATE_IDLE);
        } else {
            state_machine_transition(s_state, timestamp, REFILL_STATE_ACTIVE);
        }
    } else {
        state_machine_transition(s_state, timestamp, REFILL_STATE_STARTING);
    }
}

static void _state_starting_process(uint64_t timestamp) {
    // Wait for initial time to elapse, as per start delay
    if (timestamp - s_state.state_begin_timestamp > s_cfg->start_delay_ms) {
        if (s_current_level_ok) {
            state_machine_transition(s_state, timestamp, REFILL_STATE_IDLE);
        } else {
            state_machine_transition(s_state, timestamp, REFILL_STATE_ACTIVE);
        }
    }
}

static void _state_idle_enter(uint64_t timestamp) {
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    bool state = out_signals_get_level(OUT_SIGNALS_RELAY2);
    if (state) {
        _stop_refill();
    }

    s_level_stable_ms = 0;
}

static void _state_idle_process(uint64_t timestamp) {
    // Check hysteresis threshold
    if (s_in_error) {
        state_machine_transition(s_state, timestamp, REFILL_STATE_ERROR);
    } else if (!s_current_level_ok) {
        if (s_level_stable_ms == 0) {
            s_level_stable_ms = timestamp;
        }

        // Transition if we are over hysteresis
        if ((timestamp - s_level_stable_ms) > s_cfg->level_low_hysteresis_ms) {
            state_machine_transition(s_state, timestamp, REFILL_STATE_ACTIVE);
        }
    } else {
        s_level_stable_ms = 0;
    }

}

static void _state_active_enter(uint64_t timestamp) {
    xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);
    _start_refill();

    s_current_level_ok = 0;
}

static void _state_active_process(uint64_t timestamp) {
    // Check if we are over threshold limit
    if (s_in_error || (timestamp - s_state.state_begin_timestamp > s_cfg->max_refill_time_ms)) {
        state_machine_transition(s_state, timestamp, REFILL_STATE_ERROR);
    } else if (s_current_level_ok) {
        if (s_level_stable_ms == 0) {
            s_level_stable_ms = timestamp;
        }

        // Transition if we are over hysteresis
        if ((timestamp - s_level_stable_ms) > s_cfg->level_ok_hysteresis_ms) {
            state_machine_transition(s_state, timestamp, REFILL_STATE_IDLE);
        }
    } else {
        s_level_stable_ms = 0;
    }
}

static void _state_error_enter(uint64_t timestamp) {
    bool state = out_signals_get_level(OUT_SIGNALS_RELAY2);
    if (state) {
        ESP_LOGE(TAG, "Stopping refill, error detected");
        _stop_refill();
    }

    // Flag level as not ok
    xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);
    ESP_ERROR_CHECK(esp_event_post_to(s_event_loop, MACHINE_EVENTS, BOILER_REFILL_ERROR, nullptr, 0, portMAX_DELAY));
}

static void _state_error_process(uint64_t timestamp) {
    // If level recovers, get out of error state
    if(s_current_level_ok && !s_in_error) {
        state_machine_transition(s_state, timestamp, REFILL_STATE_IDLE);
    }
}

void boiler_refill_states_process(uint64_t timestamp_ms, bool is_level_ok, bool in_error) {
    s_current_level_ok = is_level_ok;
    s_in_error = in_error;
    state_machine_process(s_state, timestamp_ms);
}

RefillState_t boiler_refill_state() {
    return s_state.state;
}


void boiler_refill_states_power_on() {
    // Start over again, be safe and go to unknown
    state_machine_init(s_state, REFILL_STATE_UNKNOWN);
}

void boiler_refill_states_power_standby() {
    // Always stop refill
    xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);
    bool state = out_signals_get_level(OUT_SIGNALS_RELAY2);
    if (state || boiler_refill_state() == REFILL_STATE_ACTIVE) {
        _stop_refill();
    }

    // Put system in unknown state
    state_machine_init(s_state, REFILL_STATE_UNKNOWN);
}

void boiler_refill_states_init(esp_event_loop_handle_t event_loop, const boiler_refill_cfg_t &cfg) {
    s_cfg = &cfg;
    s_event_loop = event_loop;
    s_current_level_ok = false;

    // Init state machine
    s_state.mapping[REFILL_STATE_UNKNOWN].enter = _state_unknown_enter;
    s_state.mapping[REFILL_STATE_UNKNOWN].process = _state_unknown_process;

    s_state.mapping[REFILL_STATE_STARTING].process = _state_starting_process;

    s_state.mapping[REFILL_STATE_IDLE].enter = _state_idle_enter;
    s_state.mapping[REFILL_STATE_IDLE].process = _state_idle_process;

    s_state.mapping[REFILL_STATE_ACTIVE].enter = _state_active_enter;
    s_state.mapping[REFILL_STATE_ACTIVE].process = _state_active_process;

    s_state.mapping[REFILL_STATE_ERROR].enter = _state_error_enter;
    s_state.mapping[REFILL_STATE_ERROR].process = _state_error_process;

    state_machine_init(s_state, REFILL_STATE_UNKNOWN);
}
