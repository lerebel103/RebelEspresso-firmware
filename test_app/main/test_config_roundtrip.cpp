/**
 * Config JSON round-trip tests.
 *
 * These test the full config lifecycle that the web API uses:
 *   1. Create a config struct with known values
 *   2. Serialize to JSON via to_json()
 *   3. Deserialize back via from_json()
 *   4. Verify all fields match
 *
 * This validates the exact code path used by:
 *   GET /api/config/:name → to_json() → JSON response
 *   PUT /api/config/:name → from_json() → set_cfg() → NVS persist
 *
 * Also tests partial updates (only some fields in JSON) to ensure
 * unspecified fields are preserved — critical for the web UI which
 * sends back all fields including unchanged ones.
 */
#include <unity.h>
#include <freertos/FreeRTOS.h>
#include <cJSON.h>
#include <cstring>
#include <nvs_flash.h>
#include <nvs.h>

#include "utils/pid.h"
#include "hw/base/boiler_temp.h"
#include "hw/base/brew_temp.h"
#include "hw/base/boiler_refill.h"
#include "hw/base/schedules.h"

// ============================================================================
// Boiler Temp Config
// ============================================================================

TEST_CASE("Config: boiler_temp_cfg_t JSON round-trip", "[config]") {
    boiler_temp_cfg_t original = {};
    original.pid.P = 5.5;
    original.pid.I = 0.3;
    original.pid.D = 150.0;
    original.pid.I_reset_temp = 4.0;
    original.pid.setpoints[0] = 108.5;
    original.pid.setpoints[1] = 138.0;
    original.pid.over_setpoint_perc = 7.5;
    original.mains_hz = 50;
    original.temp_error_restart_time_sec = 45;
    original.full_duty_pid_error_threshold = 12;

    // Serialize
    cJSON *json = cJSON_CreateObject();
    original.to_json(json, "");

    // Deserialize into a fresh struct
    boiler_temp_cfg_t restored = {};
    restored.from_json(json);

    // Verify all fields
    TEST_ASSERT_EQUAL_DOUBLE(original.pid.P, restored.pid.P);
    TEST_ASSERT_EQUAL_DOUBLE(original.pid.I, restored.pid.I);
    TEST_ASSERT_EQUAL_DOUBLE(original.pid.D, restored.pid.D);
    TEST_ASSERT_EQUAL_DOUBLE(original.pid.I_reset_temp, restored.pid.I_reset_temp);
    TEST_ASSERT_EQUAL_DOUBLE(original.pid.setpoints[0], restored.pid.setpoints[0]);
    TEST_ASSERT_EQUAL_DOUBLE(original.pid.setpoints[1], restored.pid.setpoints[1]);
    TEST_ASSERT_EQUAL_DOUBLE(original.pid.over_setpoint_perc, restored.pid.over_setpoint_perc);
    TEST_ASSERT_EQUAL(original.mains_hz, restored.mains_hz);
    TEST_ASSERT_EQUAL(original.temp_error_restart_time_sec, restored.temp_error_restart_time_sec);
    TEST_ASSERT_EQUAL(original.full_duty_pid_error_threshold, restored.full_duty_pid_error_threshold);

    cJSON_Delete(json);
}

TEST_CASE("Config: boiler_temp partial update preserves unchanged", "[config]") {
    boiler_temp_cfg_t cfg = {};
    cfg.pid.P = 7.0;
    cfg.pid.I = 0.5;
    cfg.pid.D = 170.0;
    cfg.pid.setpoints[0] = 105.0;
    cfg.mains_hz = 50;
    cfg.temp_error_restart_time_sec = 60;

    // Partial update — only change P and mains_hz
    cJSON *partial = cJSON_CreateObject();
    cJSON_AddNumberToObject(partial, "pid.P", 9.0);
    cJSON_AddNumberToObject(partial, "mains_hz", 60);

    cfg.from_json(partial);

    // Changed
    TEST_ASSERT_EQUAL_DOUBLE(9.0, cfg.pid.P);
    TEST_ASSERT_EQUAL(60, cfg.mains_hz);
    // Unchanged
    TEST_ASSERT_EQUAL_DOUBLE(0.5, cfg.pid.I);
    TEST_ASSERT_EQUAL_DOUBLE(170.0, cfg.pid.D);
    TEST_ASSERT_EQUAL_DOUBLE(105.0, cfg.pid.setpoints[0]);
    TEST_ASSERT_EQUAL(60, cfg.temp_error_restart_time_sec);

    cJSON_Delete(partial);
}

// ============================================================================
// Brew Temp Config
// ============================================================================

TEST_CASE("Config: brew_temp_cfg_t JSON round-trip", "[config]") {
    brew_temp_cfg_t original = {};
    original.pid.P = 3.0;
    original.pid.I = 0.2;
    original.pid.D = 80.0;
    original.pid.setpoints[0] = 93.5;
    original.pid.over_setpoint_perc = 5.0;
    original.enabled = true;
    original.max_damping_perc = 12.0;
    original.boiler_setpoint_hold_sec = 180.0;

    cJSON *json = cJSON_CreateObject();
    original.to_json(json, "");

    brew_temp_cfg_t restored = {};
    restored.from_json(json);

    TEST_ASSERT_EQUAL_DOUBLE(original.pid.P, restored.pid.P);
    TEST_ASSERT_EQUAL_DOUBLE(original.pid.I, restored.pid.I);
    TEST_ASSERT_EQUAL_DOUBLE(original.pid.D, restored.pid.D);
    TEST_ASSERT_EQUAL_DOUBLE(original.pid.setpoints[0], restored.pid.setpoints[0]);
    TEST_ASSERT_TRUE(restored.enabled);
    TEST_ASSERT_EQUAL_DOUBLE(original.max_damping_perc, restored.max_damping_perc);
    TEST_ASSERT_EQUAL_DOUBLE(original.boiler_setpoint_hold_sec, restored.boiler_setpoint_hold_sec);

    cJSON_Delete(json);
}

// ============================================================================
// Boiler Refill Config
// ============================================================================

TEST_CASE("Config: boiler_refill_cfg_t JSON round-trip", "[config]") {
    boiler_refill_cfg_t original = {};
    original.start_delay_ms = 1500;
    original.stabilise_ms = 10;
    original.adc_num_readings = 30;
    original.refill_mv_threshold = 2200;
    original.max_refill_time_ms = 12000;
    original.level_low_hysteresis_ms = 800;
    original.level_ok_hysteresis_ms = 1200;

    cJSON *json = cJSON_CreateObject();
    original.to_json(json, "");

    boiler_refill_cfg_t restored = {};
    restored.from_json(json);

    TEST_ASSERT_EQUAL(original.start_delay_ms, restored.start_delay_ms);
    TEST_ASSERT_EQUAL(original.stabilise_ms, restored.stabilise_ms);
    TEST_ASSERT_EQUAL(original.adc_num_readings, restored.adc_num_readings);
    TEST_ASSERT_EQUAL(original.refill_mv_threshold, restored.refill_mv_threshold);
    TEST_ASSERT_EQUAL(original.max_refill_time_ms, restored.max_refill_time_ms);
    TEST_ASSERT_EQUAL(original.level_low_hysteresis_ms, restored.level_low_hysteresis_ms);
    TEST_ASSERT_EQUAL(original.level_ok_hysteresis_ms, restored.level_ok_hysteresis_ms);

    cJSON_Delete(json);
}

// ============================================================================
// Schedules Config
// ============================================================================

TEST_CASE("Config: schedules_cfg_t JSON round-trip", "[config]") {
    schedules_cfg_t original = {};
    original.enabled = true;

    // Set Monday schedule
    original.times[1][0].active = true;
    original.times[1][0].start_hour = 6;
    original.times[1][0].start_minute = 30;
    original.times[1][0].stop_hour = 22;
    original.times[1][0].stop_minute = 0;

    // Set Saturday schedule
    original.times[6][0].active = true;
    original.times[6][0].start_hour = 8;
    original.times[6][0].start_minute = 0;
    original.times[6][0].stop_hour = 23;
    original.times[6][0].stop_minute = 30;

    cJSON *json = cJSON_CreateObject();
    original.to_json(json, "");

    schedules_cfg_t restored = {};
    restored.from_json(json);

    TEST_ASSERT_TRUE(restored.enabled);

    // Monday
    TEST_ASSERT_TRUE(restored.times[1][0].active);
    TEST_ASSERT_EQUAL(6, restored.times[1][0].start_hour);
    TEST_ASSERT_EQUAL(30, restored.times[1][0].start_minute);
    TEST_ASSERT_EQUAL(22, restored.times[1][0].stop_hour);
    TEST_ASSERT_EQUAL(0, restored.times[1][0].stop_minute);

    // Saturday
    TEST_ASSERT_TRUE(restored.times[6][0].active);
    TEST_ASSERT_EQUAL(8, restored.times[6][0].start_hour);
    TEST_ASSERT_EQUAL(0, restored.times[6][0].start_minute);
    TEST_ASSERT_EQUAL(23, restored.times[6][0].stop_hour);
    TEST_ASSERT_EQUAL(30, restored.times[6][0].stop_minute);

    // Unset days should be inactive
    TEST_ASSERT_FALSE(restored.times[0][0].active); // Sunday
    TEST_ASSERT_FALSE(restored.times[2][0].active); // Tuesday

    cJSON_Delete(json);
}

TEST_CASE("Config: schedules JSON matches string format for print/reparse", "[config]") {
    // This tests that to_json → print → parse → from_json produces identical results
    // (verifies the JSON is well-formed and parseable)
    schedules_cfg_t cfg = {};
    cfg.enabled = true;
    cfg.times[3][0].active = true;
    cfg.times[3][0].start_hour = 7;
    cfg.times[3][0].start_minute = 15;
    cfg.times[3][0].stop_hour = 20;
    cfg.times[3][0].stop_minute = 45;

    cJSON *json1 = cJSON_CreateObject();
    cfg.to_json(json1, "");

    // Print and re-parse (simulates what NVS string storage does)
    char *str = cJSON_PrintUnformatted(json1);
    TEST_ASSERT_NOT_NULL(str);

    cJSON *json2 = cJSON_Parse(str);
    TEST_ASSERT_NOT_NULL(json2);

    schedules_cfg_t restored = {};
    restored.from_json(json2);

    TEST_ASSERT_TRUE(restored.enabled);
    TEST_ASSERT_TRUE(restored.times[3][0].active);
    TEST_ASSERT_EQUAL(7, restored.times[3][0].start_hour);
    TEST_ASSERT_EQUAL(15, restored.times[3][0].start_minute);
    TEST_ASSERT_EQUAL(20, restored.times[3][0].stop_hour);
    TEST_ASSERT_EQUAL(45, restored.times[3][0].stop_minute);

    cJSON_free(str);
    cJSON_Delete(json1);
    cJSON_Delete(json2);
}

// ============================================================================
// PID NVS persistence (simulates save/load cycle)
// ============================================================================

TEST_CASE("Config: PID config NVS save and reload", "[config]") {
    // This simulates what boiler_temp_set_cfg/init does:
    // save PID params to NVS, then load them back
    const char *NVS_NS = "test_pid_cfg";

    // Erase first
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &handle));
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);

    // Save known values
    pid_cfg_t original = {};
    original.P = 6.5;
    original.I = 0.4;
    original.D = 155.0;
    original.I_reset_temp = 4.5;
    original.setpoints[0] = 112.0;
    original.setpoints[1] = 136.0;
    original.over_setpoint_perc = 9.0;

    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &handle));
    pid_save_nvram(handle, original);
    nvs_close(handle);

    // Load into fresh struct
    pid_cfg_t loaded = {};
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &handle));
    pid_load_nvram(handle, loaded);
    nvs_close(handle);

    TEST_ASSERT_EQUAL_DOUBLE(original.P, loaded.P);
    TEST_ASSERT_EQUAL_DOUBLE(original.I, loaded.I);
    TEST_ASSERT_EQUAL_DOUBLE(original.D, loaded.D);
    TEST_ASSERT_EQUAL_DOUBLE(original.I_reset_temp, loaded.I_reset_temp);
    TEST_ASSERT_EQUAL_DOUBLE(original.setpoints[0], loaded.setpoints[0]);
    TEST_ASSERT_EQUAL_DOUBLE(original.setpoints[1], loaded.setpoints[1]);
    TEST_ASSERT_EQUAL_DOUBLE(original.over_setpoint_perc, loaded.over_setpoint_perc);
}
