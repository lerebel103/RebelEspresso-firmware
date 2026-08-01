/**
 * PID controller unit tests.
 *
 * Tests the pure algorithmic PID logic which is critical to temperature
 * control. This code has no hardware dependencies.
 */
#include <unity.h>
#include "utils/pid.h"

TEST_CASE("PID: initial state is zeroed", "[pid]") {
    pid_struct_t pid;
    pid_init(pid);

    TEST_ASSERT_EQUAL_DOUBLE(0, pid.error);
    TEST_ASSERT_EQUAL_DOUBLE(0, pid.proportional);
    TEST_ASSERT_EQUAL_DOUBLE(0, pid.integral);
    TEST_ASSERT_EQUAL_DOUBLE(0, pid.derivative);
    TEST_ASSERT_EQUAL(0, pid.last_time_us);
    TEST_ASSERT_EQUAL_DOUBLE(0, pid.last_data_value);
}

TEST_CASE("PID: first call produces no output (needs two samples)", "[pid]") {
    pid_struct_t pid;
    pid_init(pid);

    pid_cfg_t cfg = {};
    cfg.P = 7.0;
    cfg.I = 0.5;
    cfg.D = 170.0;
    cfg.setpoints[0] = 105.0;
    cfg.active_setpoint = 0;
    cfg.I_reset_temp = 5.0;
    cfg.over_setpoint_perc = 8.0;

    measure_t data = {.value = 25.0, .fault = 0};
    pid_result_t result = pid_process(pid, cfg, 1000000, data);

    // First call just stores state, doesn't compute
    TEST_ASSERT_EQUAL_DOUBLE(0, result.duty);
}

TEST_CASE("PID: proportional response to error", "[pid]") {
    pid_struct_t pid;
    pid_init(pid);

    pid_cfg_t cfg = {};
    cfg.P = 2.0;
    cfg.I = 0.0;
    cfg.D = 0.0;
    cfg.setpoints[0] = 100.0;
    cfg.active_setpoint = 0;
    cfg.I_reset_temp = 5.0;
    cfg.over_setpoint_perc = 0;

    measure_t data = {.value = 90.0, .fault = 0};

    // First sample — establishes baseline
    pid_process(pid, cfg, 1000000, data);

    // Second sample — 1 second later, same temperature
    pid_result_t result = pid_process(pid, cfg, 2000000, data);

    // Error = 100 - 90 = 10, P = 2, so proportional = 20
    // D = 0 (no change), I = 0 (disabled)
    TEST_ASSERT_EQUAL_DOUBLE(20.0, result.duty);
}

TEST_CASE("PID: over-setpoint threshold triggers", "[pid]") {
    pid_struct_t pid;
    pid_init(pid);

    pid_cfg_t cfg = {};
    cfg.P = 1.0;
    cfg.I = 0.0;
    cfg.D = 0.0;
    cfg.setpoints[0] = 100.0;
    cfg.active_setpoint = 0;
    cfg.I_reset_temp = 50.0;
    cfg.over_setpoint_perc = 5.0; // 5% over = 5 degrees

    // Temperature well above setpoint
    measure_t data = {.value = 106.0, .fault = 0};

    pid_process(pid, cfg, 1000000, data);
    pid_result_t result = pid_process(pid, cfg, 2000000, data);

    // 106 is 6 degrees over setpoint, threshold is 5 degrees
    TEST_ASSERT_TRUE(result.is_over_threshold);
}

TEST_CASE("PID: integral resets when far from setpoint", "[pid]") {
    pid_struct_t pid;
    pid_init(pid);

    pid_cfg_t cfg = {};
    cfg.P = 1.0;
    cfg.I = 1.0;
    cfg.D = 0.0;
    cfg.setpoints[0] = 100.0;
    cfg.active_setpoint = 0;
    cfg.I_reset_temp = 5.0; // Integral resets if error > 5
    cfg.over_setpoint_perc = 0;

    // Temperature far from setpoint (error = 20, > I_reset_temp of 5)
    measure_t data = {.value = 80.0, .fault = 0};

    pid_process(pid, cfg, 1000000, data);
    pid_process(pid, cfg, 2000000, data);

    // Integral should be zero because error (20) > I_reset_temp (5)
    TEST_ASSERT_EQUAL_DOUBLE(0, pid.integral);
}

TEST_CASE("PID: reset clears all state", "[pid]") {
    pid_struct_t pid;
    pid_init(pid);

    pid.error = 5.0;
    pid.proportional = 10.0;
    pid.integral = 3.0;
    pid.derivative = 2.0;
    pid.last_time_us = 999;
    pid.last_data_value = 42.0;

    pid_reset(pid);

    TEST_ASSERT_EQUAL_DOUBLE(0, pid.error);
    TEST_ASSERT_EQUAL_DOUBLE(0, pid.proportional);
    TEST_ASSERT_EQUAL_DOUBLE(0, pid.integral);
    TEST_ASSERT_EQUAL_DOUBLE(0, pid.derivative);
    TEST_ASSERT_EQUAL(0, pid.last_time_us);
    TEST_ASSERT_EQUAL_DOUBLE(0, pid.last_data_value);
}

TEST_CASE("PID: large time gap resets (> 5 seconds)", "[pid]") {
    pid_struct_t pid;
    pid_init(pid);

    pid_cfg_t cfg = {};
    cfg.P = 1.0;
    cfg.I = 1.0;
    cfg.D = 1.0;
    cfg.setpoints[0] = 100.0;
    cfg.active_setpoint = 0;
    cfg.I_reset_temp = 50.0;
    cfg.over_setpoint_perc = 0;

    measure_t data = {.value = 90.0, .fault = 0};

    // First sample
    pid_process(pid, cfg, 1000000, data);
    // Second sample, normal 1 second gap
    pid_process(pid, cfg, 2000000, data);

    // Large gap (10 seconds) — should trigger reset
    pid_result_t result = pid_process(pid, cfg, 12000000, data);
    TEST_ASSERT_EQUAL_DOUBLE(0, result.duty);
}

TEST_CASE("PID: config validation via pid_update", "[pid]") {
    pid_cfg_t dest = {};
    dest.P = 1.0;
    dest.setpoints[0] = 105.0;

    pid_cfg_t src = {};
    src.P = 5.0;
    src.I = 0.3;
    src.D = 100.0;
    src.I_reset_temp = 4.0;
    src.setpoints[0] = 120.0;
    src.setpoints[1] = 135.0;
    src.over_setpoint_perc = 10.0;

    pid_update(dest, src);

    TEST_ASSERT_EQUAL_DOUBLE(5.0, dest.P);
    TEST_ASSERT_EQUAL_DOUBLE(0.3, dest.I);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, dest.D);
    TEST_ASSERT_EQUAL_DOUBLE(4.0, dest.I_reset_temp);
    TEST_ASSERT_EQUAL_DOUBLE(120.0, dest.setpoints[0]);
    TEST_ASSERT_EQUAL_DOUBLE(135.0, dest.setpoints[1]);
    TEST_ASSERT_EQUAL_DOUBLE(10.0, dest.over_setpoint_perc);
}

TEST_CASE("PID: config validation rejects out-of-range values", "[pid]") {
    pid_cfg_t dest = {};
    dest.P = 1.0;
    dest.I = 0.5;
    dest.setpoints[0] = 105.0;
    dest.setpoints[1] = 130.0;

    pid_cfg_t src = {};
    src.P = 999.0;       // out of range (max 60)
    src.I = -1.0;        // out of range
    src.setpoints[0] = 200.0;  // out of range (max 140)
    src.setpoints[1] = 50.0;   // out of range (min 80)

    pid_update(dest, src);

    // Should remain unchanged
    TEST_ASSERT_EQUAL_DOUBLE(1.0, dest.P);
    TEST_ASSERT_EQUAL_DOUBLE(0.5, dest.I);
    TEST_ASSERT_EQUAL_DOUBLE(105.0, dest.setpoints[0]);
    TEST_ASSERT_EQUAL_DOUBLE(130.0, dest.setpoints[1]);
}
