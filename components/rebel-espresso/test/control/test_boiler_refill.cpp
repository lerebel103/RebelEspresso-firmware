#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unity.h>

#include <esp_log.h>

#include <esp_event_base.h>
#include <src/control/boiler_refill.h>

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
