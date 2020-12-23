#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unity.h>

#include <esp_log.h>
#include <esp_event.h>
#include <src/sys/nvram_store.h>

#include "control/schedules.h"
#include "control/rmt_duty_map.h"
#include "events.h"


extern esp_event_loop_handle_t g_event_loop;



TEST_CASE("[schedules:test_parse_json]", "Ensures JSON parsing works") {
    schedules_init(g_event_loop);
    schedules_reset_cfg();
    cJSON *root = cJSON_CreateObject();

    auto cfg = schedules_get_cfg();
    cfg.enabled = false;

    // Set known config
    for(int i=0; i<7; i++) {
        for (int j=0; j < DAILY_SCHEDULES_MAX; j++) {
            cfg.times[i][j].active = i % 2;
            cfg.times[i][j].start_hour = i;
            cfg.times[i][j].start_minute = j;
            cfg.times[i][j].stop_hour = i;
            cfg.times[i][j].stop_minute = j+1;
        }
    }

    // Now parse into a new guy and check they both match, done
    schedules_cfg_t new_cfg = {};
    cfg.to_json(root, "test.");

    /*
    char* content = cJSON_Print(root);
    printf("%s\r\n", content);
    cJSON_free(content);
    */

    new_cfg.from_json(root);
    TEST_ASSERT_EQUAL(cfg.enabled, new_cfg.enabled);
    for(int i=0; i<7; i++) {
        for (int j=0; j < DAILY_SCHEDULES_MAX; j++) {
            TEST_ASSERT_EQUAL(i % 2, new_cfg.times[i][j].active);
            TEST_ASSERT_EQUAL(i, new_cfg.times[i][j].start_hour);
            TEST_ASSERT_EQUAL(j, new_cfg.times[i][j].start_minute);
            TEST_ASSERT_EQUAL(i, new_cfg.times[i][j].stop_hour);
            TEST_ASSERT_EQUAL(j+1, new_cfg.times[i][j].stop_minute);
        }
    }

    cJSON_Delete(root);
    schedules_reset_cfg();
    schedules_delete();
}

TEST_CASE("[schedules:test_save_restore_cfg]", "Ensures config is saved/loaded") {
    schedules_init(g_event_loop);
    schedules_reset_cfg();

    auto cfg = schedules_get_cfg();
    cfg.enabled = false;

    // Set known config
    for(int i=0; i<7; i++) {
        for (int j=0; j < DAILY_SCHEDULES_MAX; j++) {
            cfg.times[i][j].active = i % 2;
            cfg.times[i][j].start_hour = i;
            cfg.times[i][j].start_minute = j;
            cfg.times[i][j].stop_hour = i;
            cfg.times[i][j].stop_minute = j+1;
        }
    }

    // Ok, reload
    schedules_set_cfg(cfg);
    schedules_delete();

    schedules_init(g_event_loop);
    auto new_cfg = schedules_get_cfg();

    TEST_ASSERT_EQUAL(cfg.enabled, new_cfg.enabled);
    for(int i=0; i<7; i++) {
        for (int j=0; j < DAILY_SCHEDULES_MAX; j++) {
            TEST_ASSERT_EQUAL(i % 2, new_cfg.times[i][j].active);
            TEST_ASSERT_EQUAL(i, new_cfg.times[i][j].start_hour);
            TEST_ASSERT_EQUAL(j, new_cfg.times[i][j].start_minute);
            TEST_ASSERT_EQUAL(i, new_cfg.times[i][j].stop_hour);
            TEST_ASSERT_EQUAL(j+1, new_cfg.times[i][j].stop_minute);
        }
    }

    schedules_reset_cfg();
    schedules_delete();
}


