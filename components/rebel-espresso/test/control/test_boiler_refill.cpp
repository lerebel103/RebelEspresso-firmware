#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unity.h>

#include <esp_log.h>

#include <esp_event_base.h>
#include <src/control/boiler_refill.h>
#include <events.h>
#include <esp_event.h>

extern esp_event_loop_handle_t g_event_loop;

TEST_CASE("[boiler_refill:test_update_config_from_json]", "Ensures partial json updates work") {
    boiler_refill_init(g_event_loop);
    boiler_refill_reset_cfg();
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "cfg." BOILER_REFILL_CFG_JSON_KEY "start_delay_ms", 1234);
    cJSON_AddNumberToObject(root, "cfg." BOILER_REFILL_CFG_JSON_KEY "adc_num_readings", 22);
    cJSON_AddNumberToObject(root, "cfg." BOILER_REFILL_CFG_JSON_KEY "level_low_hysteresis_ms", 50);
    cJSON_AddNumberToObject(root, "cfg." BOILER_REFILL_CFG_JSON_KEY "level_ok_hysteresis_ms", 75);


    boiler_refill_update_cfg(root);
    boiler_refill_cfg_t cfg = boiler_refill_get_cfg();
    TEST_ASSERT_EQUAL(1234, cfg.start_delay_ms);
    TEST_ASSERT_EQUAL(22, cfg.adc_num_readings);
    TEST_ASSERT_EQUAL(50, cfg.level_low_hysteresis_ms);
    TEST_ASSERT_EQUAL(75, cfg.level_ok_hysteresis_ms);

    cJSON_Delete(root);
    boiler_refill_delete();
}

TEST_CASE("[boiler_refill:test_nvs_load_save]", "Test load/save config works") {
    boiler_refill_init(g_event_loop);

    boiler_refill_cfg_t new_cfg;
    new_cfg.stabilise_ms = 21;
    new_cfg.start_delay_ms = 1234;
    new_cfg.adc_num_readings = 35;
    new_cfg.max_refill_time_ms = 3214;
    new_cfg.refill_mv_threshold = 800;
    new_cfg.level_ok_hysteresis_ms = 435;
    new_cfg.level_low_hysteresis_ms = 735;

    // Apply
    boiler_refill_set_cfg(new_cfg);

    // Now kill this instance and reload it
    boiler_refill_delete();
    boiler_refill_init(g_event_loop);

    // Get config back, it must be identical
    auto cfg = boiler_refill_get_cfg();

    TEST_ASSERT_EQUAL(new_cfg.stabilise_ms, cfg.stabilise_ms);
    TEST_ASSERT_EQUAL(new_cfg.start_delay_ms, cfg.start_delay_ms);
    TEST_ASSERT_EQUAL(new_cfg.adc_num_readings, cfg.adc_num_readings);
    TEST_ASSERT_EQUAL(new_cfg.max_refill_time_ms, cfg.max_refill_time_ms);
    TEST_ASSERT_EQUAL(new_cfg.refill_mv_threshold, cfg.refill_mv_threshold);
    TEST_ASSERT_EQUAL(new_cfg.level_ok_hysteresis_ms, cfg.level_ok_hysteresis_ms);
    TEST_ASSERT_EQUAL(new_cfg.level_low_hysteresis_ms, cfg.level_low_hysteresis_ms);

    // Now reset
    boiler_refill_reset_cfg();
    cfg = boiler_refill_get_cfg();

    TEST_ASSERT_NOT_EQUAL(new_cfg.stabilise_ms, cfg.stabilise_ms);
    TEST_ASSERT_NOT_EQUAL(new_cfg.start_delay_ms, cfg.start_delay_ms);
    TEST_ASSERT_NOT_EQUAL(new_cfg.adc_num_readings, cfg.adc_num_readings);
    TEST_ASSERT_NOT_EQUAL(new_cfg.max_refill_time_ms, cfg.max_refill_time_ms);
    TEST_ASSERT_NOT_EQUAL(new_cfg.refill_mv_threshold, cfg.refill_mv_threshold);
    TEST_ASSERT_NOT_EQUAL(new_cfg.level_ok_hysteresis_ms, cfg.level_ok_hysteresis_ms);
    TEST_ASSERT_NOT_EQUAL(new_cfg.level_low_hysteresis_ms, cfg.level_low_hysteresis_ms);

    boiler_refill_delete();
}

TEST_CASE("[boiler_refill:test_cfg_json]", "Test JSON serialisation") {
    // Formulate a JSON object, set it and make sure we get the right answer
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, BOILER_REFILL_CFG_JSON_KEY "start_delay_ms", 254);
    cJSON_AddNumberToObject(root, BOILER_REFILL_CFG_JSON_KEY "stabilise_ms", 23);
    cJSON_AddNumberToObject(root, BOILER_REFILL_CFG_JSON_KEY "adc_num_readings", 35);
    cJSON_AddNumberToObject(root, BOILER_REFILL_CFG_JSON_KEY "refill_mv_threshold", 678);
    cJSON_AddNumberToObject(root, BOILER_REFILL_CFG_JSON_KEY "max_refill_time_ms", 7567);
    cJSON_AddNumberToObject(root, BOILER_REFILL_CFG_JSON_KEY "level_low_hysteresis_ms", 139);
    cJSON_AddNumberToObject(root, BOILER_REFILL_CFG_JSON_KEY "level_ok_hysteresis_ms", 125);
    char *json = cJSON_PrintUnformatted(root);

    boiler_refill_cfg_t cfg = {};

    // Apply and get back to compare
    cfg.from_json(root);
    TEST_ASSERT_EQUAL(254, cfg.start_delay_ms);
    TEST_ASSERT_EQUAL(23, cfg.stabilise_ms);
    TEST_ASSERT_EQUAL(35, cfg.adc_num_readings);
    TEST_ASSERT_EQUAL(678, cfg.refill_mv_threshold);
    TEST_ASSERT_EQUAL(7567, cfg.max_refill_time_ms);
    TEST_ASSERT_EQUAL(139, cfg.level_low_hysteresis_ms);
    TEST_ASSERT_EQUAL(125, cfg.level_ok_hysteresis_ms);

    cJSON *new_cfg = cJSON_CreateObject();
    cfg.to_json(new_cfg, "");
    char *new_json = cJSON_PrintUnformatted(new_cfg);
    TEST_ASSERT_EQUAL_STRING(json, new_json);

    cJSON_free(json);
    cJSON_free(new_json);
    cJSON_Delete(root);
    cJSON_Delete(new_cfg);
}

static bool s_level_ok = false;
static int s_level_check_count = 0;
bool mock_check_level() {
    s_level_check_count++;
    return s_level_ok;
}

TEST_CASE("[boiler_refill:test_when_standby]", "Ensure boiler refill doesn't work in standby") {
    xEventGroupClearBits(status_event_group, POWER_ON_BIT);
    boiler_refill_init(g_event_loop);
    boiler_set_check_level_fn(mock_check_level);
    auto cfg = boiler_refill_get_cfg();

    // Zero delay
    s_level_ok = true;
    s_level_check_count = 0;
    cfg.start_delay_ms = 0;
    boiler_refill_set_cfg(cfg);

    uint64_t time_us = 0;
    for(int i=0; i<10; i++) {
        time_us += 100e3;
        ESP_ERROR_CHECK(esp_event_post_to(g_event_loop, MACHINE_EVENTS, TICK, (void *) &time_us, sizeof(uint64_t),
                                          portMAX_DELAY));
    }
    vTaskDelay(10);
    TEST_ASSERT_EQUAL(0, s_level_check_count);

    boiler_refill_reset_cfg();
    boiler_refill_delete();
}

TEST_CASE("[boiler_refill:test_power_on]", "Test works when powered on") {
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    boiler_refill_init(g_event_loop);
    boiler_set_check_level_fn(mock_check_level);

    // -----------------------------------------
    // Zero delay
    s_level_ok = true;
    s_level_check_count = 0;

    uint64_t time_us = 0;
    for(int i=0; i<10; i++) {
        time_us += 100e3;
        ESP_ERROR_CHECK(esp_event_post_to(g_event_loop, MACHINE_EVENTS, TICK, (void *) &time_us, sizeof(uint64_t),
                                          portMAX_DELAY));
    }
    vTaskDelay(50);
    TEST_ASSERT_EQUAL(10, s_level_check_count);


    boiler_refill_reset_cfg();
    boiler_refill_delete();
}


