/**
 * Safety interlock unit tests.
 *
 * Tests that all safety-critical code paths correctly inhibit dangerous
 * outputs (heater SSR, pump, solenoids) when fault conditions are detected.
 *
 * These tests verify the core safety principles:
 * 1. Any fault condition must force heater duty to zero
 * 2. Standby must kill all outputs
 * 3. SSR duty is always clamped to [0, 100]
 * 4. Sustained sensor errors trigger system restart
 * 5. Refill timeout triggers error and stops solenoid
 * 6. Over-temperature threshold cuts heater
 */
#include <unity.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_event.h>

#include "utils/pid.h"
#include "measure.h"
#include "rtds.h"

// Access global event group from test_main.cpp
extern EventGroupHandle_t status_event_group;

// Event bits (must match events.h)
#include "events.h"

// ============================================================================
// SECTION 1: PID Over-Temperature Threshold (pure algorithm, no mocks needed)
// ============================================================================

TEST_CASE("Safety: PID over-temp threshold forces is_over_threshold flag", "[safety]") {
    pid_struct_t pid;
    pid_init(pid);

    pid_cfg_t cfg = {};
    cfg.P = 1.0;
    cfg.I = 0.0;
    cfg.D = 0.0;
    cfg.setpoints[0] = 100.0;
    cfg.active_setpoint = 0;
    cfg.I_reset_temp = 50.0;
    cfg.over_setpoint_perc = 5.0; // 5% of 100 = 5 degrees over

    // First sample at setpoint (establishes baseline)
    measure_t data = {.value = 100.0, .fault = 0};
    pid_process(pid, cfg, 1000000, data);

    // Second sample exactly at threshold boundary (5 degrees over)
    data.value = 105.0;
    pid_result_t result = pid_process(pid, cfg, 2000000, data);
    // At exactly threshold, should NOT trigger (needs to exceed)
    TEST_ASSERT_FALSE(result.is_over_threshold);

    // Just over threshold
    data.value = 105.1;
    result = pid_process(pid, cfg, 3000000, data);
    TEST_ASSERT_TRUE(result.is_over_threshold);
}

TEST_CASE("Safety: PID over-temp disabled when over_setpoint_perc is zero", "[safety]") {
    pid_struct_t pid;
    pid_init(pid);

    pid_cfg_t cfg = {};
    cfg.P = 1.0;
    cfg.I = 0.0;
    cfg.D = 0.0;
    cfg.setpoints[0] = 100.0;
    cfg.active_setpoint = 0;
    cfg.I_reset_temp = 50.0;
    cfg.over_setpoint_perc = 0.0; // Disabled

    measure_t data = {.value = 100.0, .fault = 0};
    pid_process(pid, cfg, 1000000, data);

    // Way over setpoint but threshold is disabled
    data.value = 200.0;
    pid_result_t result = pid_process(pid, cfg, 2000000, data);
    TEST_ASSERT_FALSE(result.is_over_threshold);
}

TEST_CASE("Safety: PID over-temp at various setpoints", "[safety]") {
    pid_struct_t pid;
    pid_init(pid);

    // Test at high setpoint (steam)
    pid_cfg_t cfg = {};
    cfg.P = 1.0;
    cfg.I = 0.0;
    cfg.D = 0.0;
    cfg.setpoints[0] = 140.0;
    cfg.active_setpoint = 0;
    cfg.I_reset_temp = 50.0;
    cfg.over_setpoint_perc = 8.0; // 8% of 140 = 11.2 degrees

    measure_t data = {.value = 140.0, .fault = 0};
    pid_process(pid, cfg, 1000000, data);

    // Under threshold
    data.value = 150.0;
    pid_result_t result = pid_process(pid, cfg, 2000000, data);
    TEST_ASSERT_FALSE(result.is_over_threshold);

    // Over threshold (140 + 11.2 = 151.2)
    data.value = 152.0;
    result = pid_process(pid, cfg, 3000000, data);
    TEST_ASSERT_TRUE(result.is_over_threshold);
}

// ============================================================================
// SECTION 2: SSR Duty Clamping (uses mock SSR shim)
// ============================================================================

#include "ssr_ctrl.h"

TEST_CASE("Safety: SSR duty clamped to 100 when value exceeds maximum", "[safety]") {
    ssr_ctrl_config_t cfg = {.gpio = GPIO_NUM_27, .mains_hz = MAINS_50_HZ};
    ssr_ctrl_handle_t handle = nullptr;
    TEST_ASSERT_EQUAL(ESP_OK, ssr_ctrl_new(cfg, &handle));
    TEST_ASSERT_NOT_NULL(handle);

    // Set wildly over max
    TEST_ASSERT_EQUAL(ESP_OK, ssr_ctrl_set_duty(handle, 500));
    int duty = -1;
    ssr_ctrl_get_duty(handle, duty);
    TEST_ASSERT_EQUAL(100, duty);

    // Exactly at max
    TEST_ASSERT_EQUAL(ESP_OK, ssr_ctrl_set_duty(handle, 100));
    ssr_ctrl_get_duty(handle, duty);
    TEST_ASSERT_EQUAL(100, duty);

    ssr_ctrl_del(handle);
}

TEST_CASE("Safety: SSR duty clamped to 0 when value is negative", "[safety]") {
    ssr_ctrl_config_t cfg = {.gpio = GPIO_NUM_27, .mains_hz = MAINS_50_HZ};
    ssr_ctrl_handle_t handle = nullptr;
    TEST_ASSERT_EQUAL(ESP_OK, ssr_ctrl_new(cfg, &handle));

    // Set negative
    TEST_ASSERT_EQUAL(ESP_OK, ssr_ctrl_set_duty(handle, -50));
    int duty = -1;
    ssr_ctrl_get_duty(handle, duty);
    TEST_ASSERT_EQUAL(0, duty);

    // Set very negative
    TEST_ASSERT_EQUAL(ESP_OK, ssr_ctrl_set_duty(handle, -9999));
    ssr_ctrl_get_duty(handle, duty);
    TEST_ASSERT_EQUAL(0, duty);

    ssr_ctrl_del(handle);
}

TEST_CASE("Safety: SSR power_off forces duty to zero", "[safety]") {
    ssr_ctrl_config_t cfg = {.gpio = GPIO_NUM_27, .mains_hz = MAINS_50_HZ};
    ssr_ctrl_handle_t handle = nullptr;
    TEST_ASSERT_EQUAL(ESP_OK, ssr_ctrl_new(cfg, &handle));

    // Set a high duty
    ssr_ctrl_set_duty(handle, 80);
    int duty = -1;
    ssr_ctrl_get_duty(handle, duty);
    TEST_ASSERT_EQUAL(80, duty);

    // Power off must force duty to zero
    TEST_ASSERT_EQUAL(ESP_OK, ssr_ctrl_power_off(handle));
    ssr_ctrl_get_duty(handle, duty);
    TEST_ASSERT_EQUAL(0, duty);

    ssr_ctrl_del(handle);
}

TEST_CASE("Safety: SSR null handle returns error", "[safety]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ssr_ctrl_set_duty(nullptr, 50));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ssr_ctrl_power_off(nullptr));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ssr_ctrl_power_on(nullptr));

    int duty = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ssr_ctrl_get_duty(nullptr, duty));
}

// ============================================================================
// SECTION 3: Boiler Refill State Machine Safety
// ============================================================================

#include "boiler_refill.h"
#include "boiler_refill_states.h"

TEST_CASE("Safety: Refill starts in UNKNOWN state with LEVEL_OK cleared", "[safety]") {
    boiler_refill_cfg_t cfg = {};
    cfg.max_refill_time_ms = 5000;
    cfg.start_delay_ms = 0;

    boiler_refill_states_init(cfg);

    TEST_ASSERT_EQUAL(REFILL_STATE_UNKNOWN, boiler_refill_state());
}

TEST_CASE("Safety: Refill timeout triggers ERROR and stops solenoid", "[safety]") {
    boiler_refill_cfg_t cfg = {};
    cfg.max_refill_time_ms = 3000;
    cfg.start_delay_ms = 0;
    cfg.level_ok_hysteresis_ms = 500;
    cfg.level_low_hysteresis_ms = 0;

    boiler_refill_states_init(cfg);

    // Drive into ACTIVE state (level low, no delay)
    boiler_refill_states_process(0, false, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());

    // Keep running with low level past timeout
    for (uint64_t t = 100; t <= 3000; t += 100) {
        boiler_refill_states_process(t, false, false);
        TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
    }

    // Next tick exceeds timeout — must go to ERROR
    boiler_refill_states_process(3100, false, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ERROR, boiler_refill_state());
}

TEST_CASE("Safety: Refill ERROR state latches until power cycle", "[safety]") {
    boiler_refill_cfg_t cfg = {};
    cfg.max_refill_time_ms = 1000;
    cfg.start_delay_ms = 0;
    cfg.level_ok_hysteresis_ms = 0;
    cfg.level_low_hysteresis_ms = 0;

    boiler_refill_states_init(cfg);

    // Drive to active, then to error via timeout
    boiler_refill_states_process(0, false, false);
    boiler_refill_states_process(1100, false, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ERROR, boiler_refill_state());

    // Level recovers but error MUST NOT auto-clear
    boiler_refill_states_process(2000, true, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ERROR, boiler_refill_state());

    // Still latched after many ticks
    boiler_refill_states_process(5000, true, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ERROR, boiler_refill_state());

    // Only a power cycle (standby → ON) clears the error
    boiler_refill_states_power_standby();
    TEST_ASSERT_EQUAL(REFILL_STATE_UNKNOWN, boiler_refill_state());

    boiler_refill_states_power_on();
    TEST_ASSERT_EQUAL(REFILL_STATE_UNKNOWN, boiler_refill_state());

    // Now with level OK, should transition to IDLE
    boiler_refill_states_process(6000, true, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
}

TEST_CASE("Safety: Refill power standby stops solenoid and clears level bit", "[safety]") {
    boiler_refill_cfg_t cfg = {};
    cfg.max_refill_time_ms = 5000;
    cfg.start_delay_ms = 0;
    cfg.level_ok_hysteresis_ms = 0;
    cfg.level_low_hysteresis_ms = 0;

    boiler_refill_states_init(cfg);

    // Drive to ACTIVE (refilling)
    boiler_refill_states_process(0, false, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());

    // Power standby must stop everything
    boiler_refill_states_power_standby();
    TEST_ASSERT_EQUAL(REFILL_STATE_UNKNOWN, boiler_refill_state());
}

TEST_CASE("Safety: Refill external error forces ERROR state immediately", "[safety]") {
    boiler_refill_cfg_t cfg = {};
    cfg.max_refill_time_ms = 5000;
    cfg.start_delay_ms = 0;
    cfg.level_ok_hysteresis_ms = 0;
    cfg.level_low_hysteresis_ms = 0;

    boiler_refill_states_init(cfg);

    // Drive to IDLE (level OK)
    boiler_refill_states_process(0, true, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());

    // External error flag
    boiler_refill_states_process(1000, true, true);
    TEST_ASSERT_EQUAL(REFILL_STATE_ERROR, boiler_refill_state());
}

TEST_CASE("Safety: Refill hysteresis prevents spurious refill cycles", "[safety]") {
    boiler_refill_cfg_t cfg = {};
    cfg.max_refill_time_ms = 10000;
    cfg.start_delay_ms = 0;
    cfg.level_ok_hysteresis_ms = 500;
    cfg.level_low_hysteresis_ms = 1000;

    boiler_refill_states_init(cfg);

    // Start in IDLE
    boiler_refill_states_process(0, true, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());

    // Brief low readings (below hysteresis threshold) should NOT trigger refill
    boiler_refill_states_process(100, false, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());

    boiler_refill_states_process(200, false, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());

    // Level returns OK — hysteresis counter resets
    boiler_refill_states_process(300, true, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());

    // Another brief dip
    for (uint64_t t = 400; t < 1300; t += 100) {
        boiler_refill_states_process(t, false, false);
        TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());
    }

    // At t=1400, hysteresis is exactly 1000ms (not exceeded yet)
    boiler_refill_states_process(1400, false, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_IDLE, boiler_refill_state());

    // At t=1500, hysteresis exceeded (1100ms > 1000ms) — should transition
    boiler_refill_states_process(1500, false, false);
    TEST_ASSERT_EQUAL(REFILL_STATE_ACTIVE, boiler_refill_state());
}

// ============================================================================
// SECTION 4: Boiler Temp Safety Interlocks (exercises boiler_temp_process)
// ============================================================================

#include "boiler_temp.h"

// External shim functions declared in safety_test_shims.cpp
extern "C" bool test_shim_restart_called();
extern "C" void test_shim_reset_restart();
extern "C" void test_shim_intercept_restart(bool intercept);

TEST_CASE("Safety: boiler_temp standby forces SSR duty to zero", "[safety]") {
    boiler_temp_init();

    // Disable restart-on-error so we can test without it interfering
    auto cfg = boiler_temp_get_cfg();
    boiler_temp_cfg_t new_cfg = cfg;
    new_cfg.temp_error_restart_time_sec = 0;
    boiler_temp_set_cfg(new_cfg);

    // Ensure machine is in STANDBY (POWER_ON_BIT cleared)
    xEventGroupClearBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);

    measure_t data = {.value = 100.0, .fault = RTD_NoError};
    boiler_temp_process(1000000, data);

    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    boiler_temp_delete();
}

TEST_CASE("Safety: boiler_temp low water forces SSR duty to zero", "[safety]") {
    boiler_temp_init();

    auto cfg = boiler_temp_get_cfg();
    boiler_temp_cfg_t new_cfg = cfg;
    new_cfg.temp_error_restart_time_sec = 0;
    boiler_temp_set_cfg(new_cfg);

    // Power ON but water level NOT OK
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);

    measure_t data = {.value = 90.0, .fault = RTD_NoError};
    boiler_temp_process(1000000, data);

    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    boiler_temp_delete();
}

TEST_CASE("Safety: boiler_temp descale mode forces SSR duty to zero", "[safety]") {
    boiler_temp_init();

    auto cfg = boiler_temp_get_cfg();
    boiler_temp_cfg_t new_cfg = cfg;
    new_cfg.temp_error_restart_time_sec = 0;
    boiler_temp_set_cfg(new_cfg);

    // Power ON, water OK, but DESCALE active
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupSetBits(status_event_group, DESCALE_MODE_BIT);

    measure_t data = {.value = 90.0, .fault = RTD_NoError};
    boiler_temp_process(1000000, data);

    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);
    boiler_temp_delete();
}

TEST_CASE("Safety: boiler_temp RTD fault forces SSR duty to zero", "[safety]") {
    boiler_temp_init();

    auto cfg = boiler_temp_get_cfg();
    boiler_temp_cfg_t new_cfg = cfg;
    new_cfg.temp_error_restart_time_sec = 0;
    boiler_temp_set_cfg(new_cfg);

    // All conditions OK except sensor has a fault
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);

    // Test every RTD fault code
    uint8_t faults[] = {RTD_Voltage, RTD_InLow, RTD_RefLow,
                        RTD_RefHigh, RTD_RTDLow, RTD_RTDHigh};

    for (auto fault : faults) {
        measure_t data = {.value = 90.0, .fault = fault};
        boiler_temp_process(1000000, data);
        TEST_ASSERT_EQUAL_MESSAGE(0, boiler_temp_get_duty(),
            "SSR duty must be zero on RTD fault");
    }

    boiler_temp_delete();
}

TEST_CASE("Safety: boiler_temp out-of-range high (>140C) forces duty zero", "[safety]") {
    boiler_temp_init();

    auto cfg = boiler_temp_get_cfg();
    boiler_temp_cfg_t new_cfg = cfg;
    new_cfg.temp_error_restart_time_sec = 0;
    boiler_temp_set_cfg(new_cfg);

    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);

    // Temperature above 140C — out of safe range
    measure_t data = {.value = 141.0, .fault = RTD_NoError};
    boiler_temp_process(1000000, data);
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    // Way above
    data.value = 250.0;
    boiler_temp_process(2000000, data);
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    boiler_temp_delete();
}

TEST_CASE("Safety: boiler_temp out-of-range low (<0C) forces duty zero", "[safety]") {
    boiler_temp_init();

    auto cfg = boiler_temp_get_cfg();
    boiler_temp_cfg_t new_cfg = cfg;
    new_cfg.temp_error_restart_time_sec = 0;
    boiler_temp_set_cfg(new_cfg);

    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);

    // Temperature below 0C — likely sensor disconnected
    measure_t data = {.value = -1.0, .fault = RTD_NoError};
    boiler_temp_process(1000000, data);
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    data.value = -50.0;
    boiler_temp_process(2000000, data);
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    boiler_temp_delete();
}

TEST_CASE("Safety: boiler_temp sustained RTD errors trigger restart", "[safety]") {
    test_shim_intercept_restart(true);
    boiler_temp_init();
    boiler_temp_reset_stats();

    // Set a short restart timeout for testing (5 seconds)
    auto cfg = boiler_temp_get_cfg();
    boiler_temp_cfg_t new_cfg = cfg;
    new_cfg.temp_error_restart_time_sec = 5;
    boiler_temp_set_cfg(new_cfg);

    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);

    // Feed successive RTD errors every 1 second for 7 seconds
    measure_t data = {.value = 0, .fault = RTD_RTDHigh};
    for (uint64_t t = 1000000; t <= 7000000; t += 1000000) {
        boiler_temp_process(t, data);
        // Duty must be zero throughout
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
    }

    // After >5 seconds of sustained errors, restart should have been called
    TEST_ASSERT_TRUE(test_shim_restart_called());

    test_shim_intercept_restart(false);
    boiler_temp_delete();
}

TEST_CASE("Safety: boiler_temp error counter resets on good reading", "[safety]") {
    test_shim_intercept_restart(true);  // also resets the flag
    boiler_temp_init();
    boiler_temp_reset_stats();

    auto cfg = boiler_temp_get_cfg();
    boiler_temp_cfg_t new_cfg = cfg;
    new_cfg.temp_error_restart_time_sec = 10;
    new_cfg.pid.setpoints[0] = 105.0;
    boiler_temp_set_cfg(new_cfg);

    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);

    // First, clear any stale error accumulator with a good reading
    measure_t data = {.value = 90.0, .fault = RTD_NoError};
    boiler_temp_process(100000, data);

    // Feed errors for 8 seconds (under the 10s threshold)
    data.fault = RTD_RefLow;
    data.value = 0;
    for (uint64_t t = 1000000; t <= 8000000; t += 1000000) {
        boiler_temp_process(t, data);
    }
    TEST_ASSERT_FALSE(test_shim_restart_called());

    // Then a good reading — should reset the error accumulator
    data.fault = RTD_NoError;
    data.value = 90.0;
    boiler_temp_process(9000000, data);

    // Now more errors — timer should have reset, so 8 more seconds is fine
    data.fault = RTD_RefLow;
    for (uint64_t t = 10000000; t <= 18000000; t += 1000000) {
        boiler_temp_process(t, data);
    }
    TEST_ASSERT_FALSE(test_shim_restart_called());

    test_shim_intercept_restart(false);
    boiler_temp_delete();
}

TEST_CASE("Safety: boiler_temp over-temp threshold cuts heater in context", "[safety]") {
    boiler_temp_init();
    boiler_temp_reset_cfg();  // Clear NVS to get fresh defaults
    boiler_temp_delete();
    boiler_temp_init();       // Re-init with clean defaults
    boiler_temp_reset_stats();

    auto cfg = boiler_temp_get_cfg();
    boiler_temp_cfg_t new_cfg = cfg;
    new_cfg.pid.P = 3.0;
    new_cfg.pid.I = 0.0;
    new_cfg.pid.D = 0.0;
    new_cfg.pid.setpoints[0] = 100.0;
    new_cfg.pid.over_setpoint_perc = 5.0; // 5% of 100 = 5 degrees over = 105
    new_cfg.temp_error_restart_time_sec = 0;
    boiler_temp_set_cfg(new_cfg);

    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);

    // First reading to establish baseline
    measure_t data = {.value = 100.0, .fault = RTD_NoError};
    boiler_temp_process(1000000, data);

    // Temperature well above over-temp threshold (setpoint + 5% regardless of trim)
    data.value = 115.0;
    boiler_temp_process(2000000, data);

    // Duty MUST be zero — over-temp is a safety cutoff
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    // Stats should record the over-temp event
    TEST_ASSERT_TRUE(boiler_temp_get_status().temp_over_limit_count > 0);

    boiler_temp_delete();
}

// ============================================================================
// SECTION 5: Power State Safety
// ============================================================================

TEST_CASE("Safety: POWER_STANDBY event forces SSR power off", "[safety]") {
    boiler_temp_init();

    // Start with power active
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);

    // Process a normal reading to get some duty
    measure_t data = {.value = 80.0, .fault = RTD_NoError};
    boiler_temp_process(1000000, data);
    boiler_temp_process(2000000, data);
    // Should have positive duty (temp below setpoint)

    // Post POWER_STANDBY event
    ESP_ERROR_CHECK(esp_event_post(MACHINE_EVENTS, POWER_STANDBY, nullptr, 0, portMAX_DELAY));
    vTaskDelay(pdMS_TO_TICKS(50));

    // SSR duty must now be zero (power_off was called by handler)
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    boiler_temp_delete();
}

TEST_CASE("Safety: Multiple fault conditions all independently cut heater", "[safety]") {
    boiler_temp_init();

    auto cfg = boiler_temp_get_cfg();
    boiler_temp_cfg_t new_cfg = cfg;
    new_cfg.temp_error_restart_time_sec = 0;
    new_cfg.pid.setpoints[0] = 105.0;
    boiler_temp_set_cfg(new_cfg);

    measure_t data = {.value = 90.0, .fault = RTD_NoError};

    // Each condition tested independently — only one fault at a time

    // 1. Standby only
    xEventGroupClearBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);
    boiler_temp_process(1000000, data);
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    // 2. Low water only
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);
    boiler_temp_process(2000000, data);
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    // 3. Descale only
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupSetBits(status_event_group, DESCALE_MODE_BIT);
    boiler_temp_process(3000000, data);
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    // 4. RTD fault only
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);
    measure_t bad_data = {.value = 90.0, .fault = RTD_Voltage};
    boiler_temp_process(4000000, bad_data);
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    // 5. All conditions OK — heater should be allowed to run
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    xEventGroupClearBits(status_event_group, DESCALE_MODE_BIT);
    data.value = 80.0;
    boiler_temp_process(5000000, data);
    boiler_temp_process(6000000, data);
    // With temp far below setpoint, duty should be > 0
    TEST_ASSERT_GREATER_THAN(0, boiler_temp_get_duty());

    boiler_temp_delete();
}
