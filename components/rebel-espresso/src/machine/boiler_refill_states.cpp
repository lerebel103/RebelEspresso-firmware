#include <hw_config.h>
#include <esp_log.h>
#include "boiler_refill_states.h"
#include "state_machine.h"

/**
 * Boiler refill state machine — pure logic, no I/O, no blocking calls.
 *
 * The I/O scan task is responsible for:
 *   - Calling boiler_refill_states_process() every 20ms
 *   - Reading the resulting state via boiler_refill_state()
 *   - Setting relay outputs and posting events based on state transitions
 *   - Syncing event group bits from the process image
 *
 * This keeps the state machine deterministic and non-blocking.
 */

const static char *TAG = "refill";

static StateCtx_t<RefillState_t> s_state;

static bool s_current_level_ok = false;
static bool s_in_error = false;
static const boiler_refill_cfg_t *s_cfg = nullptr;
static TickType_t s_level_stable_ms = 0;

static void _state_unknown_process(uint64_t timestamp) {
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
  if (timestamp - s_state.state_begin_timestamp > s_cfg->start_delay_ms) {
    if (s_current_level_ok) {
      state_machine_transition(s_state, timestamp, REFILL_STATE_IDLE);
    } else {
      state_machine_transition(s_state, timestamp, REFILL_STATE_ACTIVE);
    }
  }
}

static void _state_idle_enter(uint64_t timestamp) {
  s_level_stable_ms = 0;
}

static void _state_idle_process(uint64_t timestamp) {
  if (s_in_error) {
    state_machine_transition(s_state, timestamp, REFILL_STATE_ERROR);
  } else if (!s_current_level_ok) {
    if (s_level_stable_ms == 0) {
      s_level_stable_ms = timestamp;
    }
    if ((timestamp - s_level_stable_ms) > s_cfg->level_low_hysteresis_ms) {
      state_machine_transition(s_state, timestamp, REFILL_STATE_ACTIVE);
    }
  } else {
    s_level_stable_ms = 0;
  }
}

static void _state_active_enter(uint64_t timestamp) {
  s_current_level_ok = false;
  s_level_stable_ms = 0;
  ESP_LOGI(TAG, "Refill ACTIVE");
}

static void _state_active_process(uint64_t timestamp) {
  if (s_in_error || (timestamp - s_state.state_begin_timestamp > s_cfg->max_refill_time_ms)) {
    state_machine_transition(s_state, timestamp, REFILL_STATE_ERROR);
  } else if (s_current_level_ok) {
    if (s_level_stable_ms == 0) {
      s_level_stable_ms = timestamp;
    }
    if ((timestamp - s_level_stable_ms) > s_cfg->level_ok_hysteresis_ms) {
      state_machine_transition(s_state, timestamp, REFILL_STATE_IDLE);
    }
  } else {
    s_level_stable_ms = 0;
  }
}

static void _state_error_enter(uint64_t timestamp) {
  ESP_LOGE(TAG, "Refill ERROR — latched until power cycle");
}

static void _state_error_process(uint64_t timestamp) {
  // Error state latches — only a power cycle (standby → ON) clears it.
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
  state_machine_init(s_state, REFILL_STATE_UNKNOWN);
}

void boiler_refill_states_power_standby() {
  state_machine_init(s_state, REFILL_STATE_UNKNOWN);
}

void boiler_refill_states_init(const boiler_refill_cfg_t& cfg) {
  s_cfg = &cfg;
  s_current_level_ok = false;

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
