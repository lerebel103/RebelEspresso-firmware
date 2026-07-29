/**
 * JSON configuration serialization tests.
 *
 * Tests the from_json/to_json round-trip on config structs. This exercises
 * cJSON which is being removed from IDF in v6.0 (moved to component registry).
 * These tests ensure the migration to the external cJSON component preserves
 * behavior.
 */
#include <unity.h>
#include <cJSON.h>
#include <cstring>
#include "utils/pid.h"

TEST_CASE("JSON: pid_cfg_t round-trip serialization", "[json]") {
    pid_cfg_t original = {};
    original.P = 4.1;
    original.I = 0.5;
    original.D = 60.4;
    original.I_reset_temp = 5.0;
    original.setpoints[0] = 107.5;
    original.setpoints[1] = 135.0;
    original.over_setpoint_perc = 8.0;

    // Serialize to JSON
    cJSON *json = cJSON_CreateObject();
    original.to_json(json, "cfg.");

    // Deserialize into a new struct
    pid_cfg_t restored = {};
    restored.from_json("cfg.", json);

    TEST_ASSERT_EQUAL_DOUBLE(original.P, restored.P);
    TEST_ASSERT_EQUAL_DOUBLE(original.I, restored.I);
    TEST_ASSERT_EQUAL_DOUBLE(original.D, restored.D);
    TEST_ASSERT_EQUAL_DOUBLE(original.I_reset_temp, restored.I_reset_temp);
    TEST_ASSERT_EQUAL_DOUBLE(original.setpoints[0], restored.setpoints[0]);
    TEST_ASSERT_EQUAL_DOUBLE(original.setpoints[1], restored.setpoints[1]);
    TEST_ASSERT_EQUAL_DOUBLE(original.over_setpoint_perc, restored.over_setpoint_perc);

    cJSON_Delete(json);
}

TEST_CASE("JSON: partial config update preserves unchanged fields", "[json]") {
    pid_cfg_t cfg = {};
    cfg.P = 7.0;
    cfg.I = 0.5;
    cfg.D = 170.0;
    cfg.setpoints[0] = 105.0;
    cfg.setpoints[1] = 140.0;

    // Only update P and setpoint0
    cJSON *partial = cJSON_CreateObject();
    cJSON_AddNumberToObject(partial, "pid.P", 9.0);
    cJSON_AddNumberToObject(partial, "pid.setpoint0", 110.0);

    cfg.from_json("", partial);

    TEST_ASSERT_EQUAL_DOUBLE(9.0, cfg.P);
    TEST_ASSERT_EQUAL_DOUBLE(110.0, cfg.setpoints[0]);
    // Unchanged fields remain
    TEST_ASSERT_EQUAL_DOUBLE(0.5, cfg.I);
    TEST_ASSERT_EQUAL_DOUBLE(170.0, cfg.D);
    TEST_ASSERT_EQUAL_DOUBLE(140.0, cfg.setpoints[1]);

    cJSON_Delete(partial);
}

TEST_CASE("JSON: cJSON create and parse basic object", "[json]") {
    // Verify cJSON fundamentals work (catches library linking issues)
    cJSON *root = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(root);

    cJSON_AddNumberToObject(root, "temperature", 98.6);
    cJSON_AddStringToObject(root, "unit", "celsius");
    cJSON_AddBoolToObject(root, "active", true);

    // Print and re-parse
    char *str = cJSON_PrintUnformatted(root);
    TEST_ASSERT_NOT_NULL(str);

    cJSON *parsed = cJSON_Parse(str);
    TEST_ASSERT_NOT_NULL(parsed);

    cJSON *temp = cJSON_GetObjectItem(parsed, "temperature");
    TEST_ASSERT_NOT_NULL(temp);
    TEST_ASSERT_EQUAL_DOUBLE(98.6, temp->valuedouble);

    cJSON *unit = cJSON_GetObjectItem(parsed, "unit");
    TEST_ASSERT_NOT_NULL(unit);
    TEST_ASSERT_EQUAL_STRING("celsius", cJSON_GetStringValue(unit));

    cJSON *active = cJSON_GetObjectItem(parsed, "active");
    TEST_ASSERT_NOT_NULL(active);
    TEST_ASSERT_TRUE(cJSON_IsTrue(active));

    cJSON_free(str);
    cJSON_Delete(root);
    cJSON_Delete(parsed);
}

TEST_CASE("JSON: cJSON array handling", "[json]") {
    // Test array creation/parsing — used by schedules config
    cJSON *root = cJSON_CreateObject();
    cJSON *array = cJSON_AddArrayToObject(root, "items");

    for (int i = 0; i < 5; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "idx", i);
        cJSON_AddBoolToObject(item, "en", i % 2 == 0);
        cJSON_AddItemToArray(array, item);
    }

    // Verify
    TEST_ASSERT_EQUAL(5, cJSON_GetArraySize(array));

    for (int i = 0; i < 5; i++) {
        cJSON *item = cJSON_GetArrayItem(array, i);
        TEST_ASSERT_EQUAL(i, cJSON_GetObjectItem(item, "idx")->valueint);
        TEST_ASSERT_EQUAL(i % 2 == 0, cJSON_IsTrue(cJSON_GetObjectItem(item, "en")));
    }

    cJSON_Delete(root);
}
