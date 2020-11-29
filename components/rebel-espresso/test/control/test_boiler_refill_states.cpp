#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unity.h>

#include <esp_log.h>

#include <esp_event_base.h>
#include <src/control/boiler_refill.h>
#include <events.h>
#include <esp_event.h>
#include <src/control/boiler_refill_states.h>
#include <hw_config.h>

extern esp_event_loop_handle_t g_event_loop;

static int refill_start_count = 0;
static int refill_stopped_count = 0;
static int refill_error_count = 0;

static void _refill_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == BOILER_REFILL_STARTED) {
        refill_start_count++;
    } else if (id == BOILER_REFILL_STOPPED) {
        refill_stopped_count++;
    } else if (id == BOILER_REFILL_ERROR) {
        refill_error_count++;
    }
}

static void _reset_test_counters() {
    refill_start_count = 0;
    refill_stopped_count = 0;
    refill_error_count = 0;

    // Ensures handlers are registerd
    ESP_ERROR_CHECK(esp_event_handler_register_with(g_event_loop, MACHINE_EVENTS, BOILER_REFILL_STARTED,
                                                    _refill_events, g_event_loop));

    ESP_ERROR_CHECK(esp_event_handler_register_with(g_event_loop, MACHINE_EVENTS, BOILER_REFILL_STOPPED,
                                                    _refill_events, g_event_loop));

    ESP_ERROR_CHECK(esp_event_handler_register_with(g_event_loop, MACHINE_EVENTS, BOILER_REFILL_ERROR,
                                                    _refill_events, g_event_loop));

}

TEST_CASE("[boiler_refill_states:test_initial_state]", "Ensures initial state valid") {
    boiler_refill_cfg_t new_cfg = { };

    boiler_refill_states_init(g_event_loop, new_cfg);
    TEST_ASSERT_EQUAL(REFILL_STATE_UNKNOWN, boiler_refill_state());
}

TEST_CASE("[boiler_refill_states:test_power_events]", "Ensure power events reset/start state machine") {
    _reset_test_counters();
    boiler_refill_cfg_t new_cfg = {};
    new_cfg.max_refill_time_ms = 5000;

    uint64_t time_ms = 0;
    boiler_refill_states_init(g_event_loop, new_cfg);

    for (int j=0; j<5; j++) {
        boiler_refill_states_power_on();
        for (int i = 0; i < 10; i++) {
            time_ms += 100;
            boiler_refill_states_process(time_ms, true);
            vTaskDelay(1);
            TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
            TEST_ASSERT_TRUE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
            TEST_ASSERT_FALSE((GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is off
        }

        boiler_refill_states_power_standby();
        TEST_ASSERT_EQUAL(REFILL_STATE_UNKNOWN, boiler_refill_state());
        TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
        TEST_ASSERT_FALSE((GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is off
    }
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

TEST_CASE("[boiler_refill_states:test_state_ok_unstable_then_low_stable]", "Ensures boiler refills only when low stable") {
    _reset_test_counters();
    boiler_refill_cfg_t new_cfg = {};
    new_cfg.max_refill_time_ms = 5000;
    new_cfg.level_low_hysteresis_ms = 1200;

    uint64_t time_ms = 0;
    boiler_refill_states_init(g_event_loop, new_cfg);

    boiler_refill_states_process(0, true);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
    TEST_ASSERT_TRUE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
    TEST_ASSERT_FALSE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is off

    // Flip every second reading to make it unstable
    bool ok = false;
    for(time_ms=100; time_ms<10000; time_ms+=100) {
        boiler_refill_states_process(time_ms, ok);
        ok = !ok;
        vTaskDelay(1);
        TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
        TEST_ASSERT_TRUE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
        TEST_ASSERT_FALSE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is off
    }

    // Then feed stable low
    auto target = time_ms + new_cfg.level_low_hysteresis_ms;
    for(; time_ms<target; time_ms+=100) {
        boiler_refill_states_process(time_ms, false);
        vTaskDelay(1);
        TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
        TEST_ASSERT_TRUE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
        TEST_ASSERT_FALSE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is off
    }

    TEST_ASSERT_EQUAL(0, refill_start_count);
    TEST_ASSERT_EQUAL(0, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);


    // And this very next call tips it now
    time_ms += 100;
    boiler_refill_states_process(time_ms, false);
    vTaskDelay(1);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
    TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
    TEST_ASSERT_TRUE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is on

    TEST_ASSERT_EQUAL(1, refill_start_count);
    TEST_ASSERT_EQUAL(0, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);

    // and again for the hell of it
    time_ms += 100;
    boiler_refill_states_process(time_ms, false);
    vTaskDelay(1);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
    TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
    TEST_ASSERT_TRUE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is on

    TEST_ASSERT_EQUAL(1, refill_start_count);
    TEST_ASSERT_EQUAL(0, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);

}

TEST_CASE("[boiler_refill_states:test_state_low_unstable_then_stable]", "Ensures boiler stops refilling when stable") {
    _reset_test_counters();
    boiler_refill_cfg_t new_cfg = {};
    new_cfg.max_refill_time_ms = 5000;
    new_cfg.level_ok_hysteresis_ms = 1100;

    uint64_t time_ms = 0;
    boiler_refill_states_init(g_event_loop, new_cfg);

    boiler_refill_states_process(0, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
    TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
    TEST_ASSERT_TRUE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is on

    // Flip every second reading to make it unstable for 3 seconds
    // In this case it keeps filling steady
    bool ok = true;
    for(time_ms=100; time_ms<3000; time_ms+=100) {
        boiler_refill_states_process(time_ms, ok);
        ok = !ok;
        vTaskDelay(1);
        TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
        TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
        TEST_ASSERT_TRUE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is on
    }

    // Then feed stable low
    auto target = time_ms + new_cfg.level_ok_hysteresis_ms;
    for(; time_ms<target; time_ms+=100) {
        boiler_refill_states_process(time_ms, true);
        vTaskDelay(1);
        TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
        TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
        TEST_ASSERT_TRUE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is on
    }

    TEST_ASSERT_EQUAL(1, refill_start_count);
    TEST_ASSERT_EQUAL(0, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);


    // And this very next call tips it now
    time_ms += 100;
    boiler_refill_states_process(time_ms, true);
    vTaskDelay(1);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
    TEST_ASSERT_TRUE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
    TEST_ASSERT_FALSE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is off

    TEST_ASSERT_EQUAL(1, refill_start_count);
    TEST_ASSERT_EQUAL(1, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);

    // and again for the hell of it
    time_ms += 100;
    boiler_refill_states_process(time_ms, true);
    vTaskDelay(1);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
    TEST_ASSERT_FALSE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is off

    TEST_ASSERT_EQUAL(1, refill_start_count);
    TEST_ASSERT_EQUAL(1, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);
}

TEST_CASE("[boiler_refill_states:test_refill_over_time]", "Ensure refill times out and errors") {
    _reset_test_counters();
    boiler_refill_cfg_t new_cfg = {};
    new_cfg.max_refill_time_ms = 5000;

    uint64_t time_ms = 0;
    boiler_refill_states_init(g_event_loop, new_cfg);
    TEST_ASSERT_EQUAL(0, refill_start_count);
    TEST_ASSERT_EQUAL(0, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);

    boiler_refill_states_process(0, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
    TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
    TEST_ASSERT_TRUE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is on

    TEST_ASSERT_EQUAL(1, refill_start_count);
    TEST_ASSERT_EQUAL(0, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);

    // Flip every second reading to make it unstable for 3 seconds
    // In this case it keeps filling steady
    for(time_ms=100; time_ms<5000; time_ms+=100) {
        boiler_refill_states_process(time_ms, false);
        vTaskDelay(1);
        TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
        TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
        TEST_ASSERT_TRUE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is on
    }

    TEST_ASSERT_EQUAL(1, refill_start_count);
    TEST_ASSERT_EQUAL(0, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);

    // And this very next call tips it into timeout
    time_ms += 100;
    boiler_refill_states_process(time_ms, false);
    vTaskDelay(1);
    TEST_ASSERT_EQUAL(REFILL_STATE_ERROR, boiler_refill_state());
    TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
    TEST_ASSERT_FALSE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is off

    TEST_ASSERT_EQUAL(1, refill_start_count);
    TEST_ASSERT_EQUAL(1, refill_stopped_count);
    TEST_ASSERT_EQUAL(1, refill_error_count);
}

TEST_CASE("[boiler_refill_states:test_state_error]", "Ensure error state latches") {
    _reset_test_counters();
    boiler_refill_cfg_t new_cfg = {};
    new_cfg.max_refill_time_ms = 5000;

    uint64_t time_ms = 0;
    boiler_refill_states_init(g_event_loop, new_cfg);
    TEST_ASSERT_EQUAL(0, refill_start_count);
    TEST_ASSERT_EQUAL(0, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);

    boiler_refill_states_process(0, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
    TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
    TEST_ASSERT_TRUE( (GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is on

    TEST_ASSERT_EQUAL(1, refill_start_count);
    TEST_ASSERT_EQUAL(0, refill_stopped_count);
    TEST_ASSERT_EQUAL(0, refill_error_count);

    // Then cause error, and we remain in error
    time_ms = new_cfg.max_refill_time_ms + 100;
    boiler_refill_states_process(time_ms, false);

    for(int i=0; i<20; i++) {
        time_ms += 100;
        boiler_refill_states_process(time_ms, false);
        TEST_ASSERT_EQUAL(REFILL_STATE_ERROR, boiler_refill_state());
        TEST_ASSERT_FALSE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
        TEST_ASSERT_FALSE((GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is off

        TEST_ASSERT_EQUAL(1, refill_start_count);
        TEST_ASSERT_EQUAL(1, refill_stopped_count);
        TEST_ASSERT_EQUAL(1, refill_error_count);
    }

    // OK, we get out of this state if level recovers
    boiler_refill_states_process(time_ms, true);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
    TEST_ASSERT_TRUE(xEventGroupGetBits(status_event_group) & BOILER_LEVEL_OK_BIT);
    TEST_ASSERT_FALSE((GPIO_REG_READ(GPIO_OUT_REG) >> GPIO_TRIG2_REL2) & 1U); // Refill solenoid is off

}
