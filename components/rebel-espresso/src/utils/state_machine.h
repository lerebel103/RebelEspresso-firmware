#pragma once

#include <ctime>
#include <functional>
#include <map>
#include <esp_log.h>

#define SM_LOG_TAG "state_machine"

/**
 * Generic and simple implementation of a State Machine.
 *
 * This implementation is templatized and needs to have a set of States defined as an enumeration, such as:
 *
 * enum MyState_t {
 *   STATE_A,
 *   STATE_B
 * }
 *
 * From there, you define mappings for a given State to a specific function and initialise a context:
 *
 *   StatesMapping<MpuState_t > mappings;
 *   mappings[MPU_STATE_UNKNOWN] = handle_state_A;
 *   mappings[MPU_STATE_ACTIVE] = handle_state_B;
 *
 *   StateCtx_t ctx;
 *   mpu_state_init(ctx, STATE_A, mappings);
 *
 * Then you are off, mpu_state_process() is called as part of your handling and the correct state handler is then
 * called.
 */

// Generic State handler that takes in the current time
typedef std::function<void(time_t)> StateHandlerFun;

namespace {
    void null_state_handler(time_t) {}
}


struct StateHandler {

    /**
     * Called when state is entered
     */
    StateHandlerFun enter = null_state_handler;

    /**
     * Main state handler
     */
    StateHandlerFun process = null_state_handler;

    /**
     * Called when state is exited
     */
    StateHandlerFun exit = null_state_handler;
};

// Mapping of State to Hanlder functions
template <typename State_t> using StatesMapping = std::map<State_t, StateHandler>;

/**
 * Context object that helps us keep track of a specific MPU's state.
 */
template <typename State_t> struct StateCtx_t {
    /**
     * Time on which this state was entered
     */
    time_t state_begin_timestamp;

    /**
     * Current state
     */
    State_t state;

    /**
     * Mappings of states to handlers
     */
    StatesMapping<State_t> mapping;
};

/**
 * Initialises the given context.
 */
template <typename State_t> void state_machine_init(StateCtx_t<State_t> &ctx, State_t initial) {
    ctx.state_begin_timestamp = 0;
    ctx.state = initial;
    ESP_LOGD(SM_LOG_TAG, "State Machine initialised.");
}

/**
 * Transitions from current state to a new given state.
 * @param ctx
 * @param timestamp
 * @param new_state
 */
template <typename State_t> void state_machine_transition(StateCtx_t<State_t> &ctx, time_t timestamp, State_t new_state) {
    if (ctx.mapping.find(new_state) != ctx.mapping.end()) {
        ESP_LOGI(SM_LOG_TAG, "Transitioning from %s->%s", state_to_str(ctx.state), state_to_str(new_state));
        ctx.state_begin_timestamp = timestamp;

        // Announce end of current state
        ctx.mapping[ctx.state].exit(timestamp);

        // Set and announce begin of new state
        ctx.state = new_state;
        ctx.mapping[ctx.state].enter(timestamp);

    } else {
        ESP_LOGI(SM_LOG_TAG, "No transition defined %s->%s", state_to_str(ctx.state), state_to_str(new_state));
    }

}

/**
 * Entry point, delegates to the current state and calculates new transitions as desired.
 */
template <typename State_t> void state_machine_process(StateCtx_t<State_t> &ctx, time_t timestamp) {
    ESP_LOGD(SM_LOG_TAG, "[STATE %s]", state_to_str(ctx.state));

    if (ctx.mapping.find(ctx.state) != ctx.mapping.end()) {
        ctx.mapping[ctx.state].process(timestamp);
    } else {
        ESP_LOGE(SM_LOG_TAG, "No state mapping defined for %s, noop.", state_to_str(ctx.state));
    }
}
