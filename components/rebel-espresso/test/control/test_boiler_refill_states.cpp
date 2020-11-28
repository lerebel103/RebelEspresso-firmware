#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unity.h>

#include <esp_log.h>

#include <esp_event_base.h>
#include <src/control/boiler_refill.h>
#include <events.h>
#include <esp_event.h>
#include <src/control/boiler_refill_states.h>

extern esp_event_loop_handle_t g_event_loop;

TEST_CASE("[boiler_refill_states:test_initial_state]", "Ensures initial state valid") {
    boiler_refill_cfg_t new_cfg = { };
    
    boiler_refill_states_init(g_event_loop, new_cfg);
    TEST_ASSERT_EQUAL(REFILL_STATE_UNKNOWN, boiler_refill_state());
    
}

TEST_CASE("[boiler_refill_states:test_state_starting_low]", "Ensures Starting state works when low") {
    boiler_refill_cfg_t new_cfg = {
            .start_delay_ms = 1234,
            .stabilise_ms = 21,
            .adc_num_readings = 35,
            .refill_mv_threshold = 800,
            .max_refill_time_ms = 3214,
            .level_low_hysteresis_ms = 735,
            .level_ok_hysteresis_ms = 435,
    };

    boiler_refill_states_init(g_event_loop, new_cfg);
    boiler_refill_states_process(0, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_STARTING, boiler_refill_state());

    for(int i=1220; i<1235; i++) {
        boiler_refill_states_process(i, false);
        TEST_ASSERT_EQUAL(REFILL_STATE_STARTING, boiler_refill_state());
    }

    // Tip over the edge, since low level is detected we go to active
    boiler_refill_states_process(1235, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
}

TEST_CASE("[boiler_refill_states:test_state_starting_ok]", "Ensures Starting state works when ok") {
    boiler_refill_cfg_t new_cfg = {
            .start_delay_ms = 1234,
            .stabilise_ms = 21,
            .adc_num_readings = 35,
            .refill_mv_threshold = 800,
            .max_refill_time_ms = 3214,
            .level_low_hysteresis_ms = 735,
            .level_ok_hysteresis_ms = 435,
    };

    boiler_refill_states_init(g_event_loop, new_cfg);
    boiler_refill_states_process(0, true);
    TEST_ASSERT_EQUAL(REFILL_STATE_STARTING, boiler_refill_state());

    for(int i=1220; i<1235; i++) {
        boiler_refill_states_process(i, true);
        TEST_ASSERT_EQUAL(REFILL_STATE_STARTING, boiler_refill_state());
    }

    // Tip over the edge, since low level is detected we go to active
    boiler_refill_states_process(1235, true);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
}

TEST_CASE("[boiler_refill_states:test_state_no_delay_low]", "Ensures Starting state works when low and zero delay") {
    boiler_refill_cfg_t new_cfg = {};
    new_cfg.max_refill_time_ms = 5000;

    boiler_refill_states_init(g_event_loop, new_cfg);
    boiler_refill_states_process(0, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());

    // Tip over the edge, since low level is detected we go to active
    boiler_refill_states_process(1235, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
}

TEST_CASE("[boiler_refill_states:test_state_no_delay_ok]", "Ensures Starting state works when ok and zero delay") {
    boiler_refill_cfg_t new_cfg = {};
    new_cfg.max_refill_time_ms = 5000;

    boiler_refill_states_init(g_event_loop, new_cfg);
    boiler_refill_states_process(0, true);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());

    // Tip over the edge, since low level is detected we go to active
    boiler_refill_states_process(1235, true);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
}
