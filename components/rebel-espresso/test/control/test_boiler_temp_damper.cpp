#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unity.h>

#include <esp_log.h>
#include <esp_event.h>
#include <src/sys/nvram_store.h>

#include "control/boiler_temp_damper.h"
#include "control/rmt_duty_map.h"
#include "events.h"

extern "C" void boiler_temp_damper_set_duty(uint8_t duty);

extern esp_event_loop_handle_t g_event_loop;

TEST_CASE("[boiler_temp_damper:test_standby_duty_max]", "Ensure STANDBY cuts duty to zero") {
    boiler_temp_damper_init(g_event_loop);

    // Turn off restart on RTD error
    auto cfg = boiler_temp_damper_get_cfg();
    cfg.enabled = true;
    boiler_temp_damper_set_cfg(cfg);

    // Not enabled by default, zero duty
    TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());

    // Fake 100% duty to turn on HBRIDGE
    uint8_t duty = 0;
    boiler_temp_damper_set_duty(duty);
    TEST_ASSERT_EQUAL(duty, boiler_temp_damper_get_duty());
    vTaskDelay(pdMS_TO_TICKS(200));


    // Generate a standby event, we have zero power
    ESP_ERROR_CHECK(esp_event_post_to(g_event_loop, MACHINE_EVENTS, POWER_STANDBY, nullptr, 0, portMAX_DELAY));

    // Now HBRIDGE must be powered off.
    TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());

    boiler_temp_damper_delete();
}


TEST_CASE("[boiler_temp_damper:test_error_conditions]", "Ensure process cuts power when conditions not met") {
    boiler_temp_damper_init(g_event_loop);
    boiler_temp_damper_reset_stats();

    // Turn off restart on RTD error
    auto cfg = boiler_temp_damper_get_cfg();
    cfg.enabled = true;
    boiler_temp_damper_set_cfg(cfg);

    // Not enabled by default, duty maxed out
    TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());


    // Observe that it is dropped back to 0 forcefully when boiler_temp_damper is disabled
    // + duty is set to zero
    boiler_temp_damper_set_duty(0);

    for (int i = 0; i < 10; i++) {
        // --  Standby
        // Mark as standby mode and generate a tick process loop
        xEventGroupClearBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        rtd_data_t data;
        data.fault = Max31865Error::NoError;
        boiler_temp_damper_process(esp_timer_get_time(), data);

        // Now HBRIDGE must be powered off.
        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());

        // --  Brew level not good
        // Brew empty but power active
        boiler_temp_damper_set_duty(0);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::NoError;
        boiler_temp_damper_process(esp_timer_get_time(), data);

        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());

        // --  RTD broken, High
        // Brew empty but power active
        boiler_temp_damper_set_duty(0);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RTDHigh;
        boiler_temp_damper_process(esp_timer_get_time(), data);

        // Now HBRIDGE must be powered off.
        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());

        // --  RTD broken, Low
        // Brew empty but power active
        boiler_temp_damper_set_duty(0);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RTDLow;
        boiler_temp_damper_process(esp_timer_get_time(), data);

        // Now HBRIDGE must be powered off.
        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());

        // --  RTD broken, RTDInLow
        // Brew empty but power active
        boiler_temp_damper_set_duty(0);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RTDInLow;
        boiler_temp_damper_process(esp_timer_get_time(), data);

        // Now HBRIDGE must be powered off.
        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());

        // --  RTD broken, RefHigh
        // Brew empty but power active
        boiler_temp_damper_set_duty(0);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RefHigh;
        boiler_temp_damper_process(esp_timer_get_time(), data);

        // Now HBRIDGE must be powered off.
        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());

        // --  RTD broken, RefLow
        // Brew empty but power active
        boiler_temp_damper_set_duty(0);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RefLow;
        boiler_temp_damper_process(esp_timer_get_time(), data);

        // Now HBRIDGE must be powered off.
        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());

        // --  RTD broken, Ref high
        // Brew empty but power active
        boiler_temp_damper_set_duty(0);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RefHigh;
        boiler_temp_damper_process(esp_timer_get_time(), data);

        // Now HBRIDGE must be powered off.
        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());

        // --  RTD broken, voltage
        // Brew empty but power active
        boiler_temp_damper_set_duty(0);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::Voltage;
        boiler_temp_damper_process(esp_timer_get_time(), data);

        // Now HBRIDGE must be powered off.
        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());
    }

    // With above tests we expected this many temp read erros
    TEST_ASSERT_EQUAL(70, boiler_temp_damper_get_status().brew_temp_read_error_count);

    boiler_temp_damper_delete();
}


TEST_CASE("[boiler_temp_damper:test_update_config_from_json]", "Ensures partial json updates work") {
    boiler_temp_damper_init(g_event_loop);
    boiler_temp_damper_reset_cfg();
    cJSON *root = cJSON_CreateObject();

    cJSON_AddBoolToObject(root, "cfg." BOILER_DAMPER_CFG_JSON_KEY "enabled", true);
    cJSON_AddNumberToObject(root, "cfg." BOILER_DAMPER_CFG_JSON_KEY "pid.P", 4.1);
    cJSON_AddNumberToObject(root, "cfg." BOILER_DAMPER_CFG_JSON_KEY "pid.I", 0.5);
    cJSON_AddNumberToObject(root, "cfg." BOILER_DAMPER_CFG_JSON_KEY "pid.D", 60.4);
    cJSON_AddNumberToObject(root, "cfg." BOILER_DAMPER_CFG_JSON_KEY "pid.setpoint0", 99.9);
    cJSON_AddNumberToObject(root, "cfg." BOILER_DAMPER_CFG_JSON_KEY "max_damping_perc", 1.34);
    cJSON_AddNumberToObject(root, "cfg." BOILER_DAMPER_CFG_JSON_KEY "reset_time_sec", 34);

    boiler_temp_damper_update_cfg(root);
    boiler_temp_damper_cfg_t cfg = boiler_temp_damper_get_cfg();
    TEST_ASSERT_EQUAL(true, cfg.enabled);
    TEST_ASSERT_EQUAL(4.1, cfg.pid.P);
    TEST_ASSERT_EQUAL(0.5, cfg.pid.I);
    TEST_ASSERT_EQUAL(60.4, cfg.pid.D);
    TEST_ASSERT_EQUAL(99.9, cfg.pid.setpoints[0]);
    TEST_ASSERT_EQUAL(1.34, cfg.max_damping_perc);
    TEST_ASSERT_EQUAL(34, cfg.reset_time_sec);

    cJSON_Delete(root);
    boiler_temp_damper_delete();
}


TEST_CASE("[boiler_temp_damper:test_nvs_load_save]", "Test load/save config works") {
    boiler_temp_damper_init(g_event_loop);

    boiler_temp_damper_cfg_t new_cfg;
    new_cfg.enabled = true;
    new_cfg.pid.P = 5.5;
    new_cfg.pid.I = 5.6;
    new_cfg.pid.D = 5.7;
    new_cfg.pid.I_reset_temp = 5.8;
    new_cfg.pid.I_reset_sec = 6;
    new_cfg.pid.setpoints[0] = 51.2;
    new_cfg.pid.over_setpoint_perc = 13.1;
    new_cfg.pid.min_duty_band = 6.1;
    new_cfg.max_damping_perc = 2.3;
    new_cfg.reset_time_sec = 35;

    // Apply
    boiler_temp_damper_set_cfg(new_cfg);

    // Now kill this instance and reload it
    boiler_temp_damper_delete();
    boiler_temp_damper_init(g_event_loop);

    // Get config back, it must be identical
    auto cfg = boiler_temp_damper_get_cfg();

    TEST_ASSERT_EQUAL(new_cfg.enabled, cfg.enabled);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.P, cfg.pid.P);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.I, cfg.pid.I);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.D, cfg.pid.D);
    TEST_ASSERT_EQUAL(new_cfg.pid.I_reset_sec, cfg.pid.I_reset_sec);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.I_reset_temp, cfg.pid.I_reset_temp);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.setpoints[0], cfg.pid.setpoints[0]);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.over_setpoint_perc, cfg.pid.over_setpoint_perc);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.min_duty_band, cfg.pid.min_duty_band);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.max_damping_perc, cfg.max_damping_perc);
    TEST_ASSERT_EQUAL(new_cfg.reset_time_sec, cfg.reset_time_sec);

    boiler_temp_damper_delete();
}


TEST_CASE("[boiler_temp_damper:test_nvs_reset_default]", "Test resetting NVS to defaults") {
    // reset
    boiler_temp_damper_reset_cfg();
    boiler_temp_damper_init(g_event_loop);

    // Formulate a JSON object, set it and make sure we get the right answer
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "pid.P", 4.1);
    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "pid.I", 0.5);
    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "pid.D", 60.4);
    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "pid.I_reset_sec", 16);
    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "pid.I_reset_temp", 14);
    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "pid.setpoint0", 125);
    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "pid.setpoint1", 139);
    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "pid.over_setpoint_perc", 25);
    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "pid.min_duty_band", 8);
    cJSON_AddBoolToObject(root, BOILER_DAMPER_CFG_JSON_KEY "enabled", true);
    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "max_damping_perc", 3.1);
    cJSON_AddNumberToObject(root, BOILER_DAMPER_CFG_JSON_KEY "reset_time_sec", 1);
    char *json = cJSON_PrintUnformatted(root);

    boiler_temp_damper_cfg_t cfg = {};

    // Apply and get back to compare
    cfg.from_json(root);
    TEST_ASSERT_EQUAL(true, cfg.enabled);
    TEST_ASSERT_EQUAL(4.1, cfg.pid.P);
    TEST_ASSERT_EQUAL(0.5, cfg.pid.I);
    TEST_ASSERT_EQUAL(60.4, cfg.pid.D);
    TEST_ASSERT_EQUAL(16, cfg.pid.I_reset_sec);
    TEST_ASSERT_EQUAL(14, cfg.pid.I_reset_temp);
    TEST_ASSERT_EQUAL(125, cfg.pid.setpoints[0]);
    TEST_ASSERT_EQUAL(139, cfg.pid.setpoints[1]);
    TEST_ASSERT_EQUAL(25, cfg.pid.over_setpoint_perc);
    TEST_ASSERT_EQUAL(8, cfg.pid.min_duty_band);
    TEST_ASSERT_EQUAL(3.1, cfg.max_damping_perc);
    TEST_ASSERT_EQUAL(1, cfg.reset_time_sec);

    cJSON *new_cfg = cJSON_CreateObject();
    cfg.to_json(new_cfg, "");
    char *new_json = cJSON_PrintUnformatted(new_cfg);
    TEST_ASSERT_EQUAL_STRING(json, new_json);

    cJSON_free(json);
    cJSON_free(new_json);
    cJSON_Delete(root);
    cJSON_Delete(new_cfg);

    boiler_temp_damper_delete();
}



TEST_CASE("[boiler_temp_damper:test_brew_duty_steady_pos]", "Test duty when steady temp under setpoint fed") {
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    boiler_temp_damper_init(g_event_loop);
    boiler_temp_damper_reset_cfg();

    // Turn off restart on RTD error
    auto cfg = boiler_temp_damper_get_cfg();
    cfg.enabled = true;
    cfg.pid.active_setpoint = 0;
    cfg.pid.setpoints[0] = 92;
    boiler_temp_damper_set_cfg(cfg);

    rtd_data_t data = {};
    data.fault = Max31865Error::NoError;
    data.temperature = 25;

    // Maxes out duty in this case
    for (int i = 0; i < 10; i++) {
        boiler_temp_damper_process(i * 1e6, data);
        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());
    }

    boiler_temp_damper_delete();
}

TEST_CASE("[boiler_temp_damper:test_brew_duty_steady_neg]", "Test duty when steady temp over setpoint fed") {
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    boiler_temp_damper_init(g_event_loop);
    boiler_temp_damper_reset_cfg();

    // Turn off restart on RTD error
    auto cfg = boiler_temp_damper_get_cfg();
    cfg.enabled = true;
    cfg.pid.over_setpoint_perc = 0;
    cfg.pid.active_setpoint = 0;
    cfg.pid.setpoints[0] = 50;
    cfg.pid.I_reset_temp = 1;
    boiler_temp_damper_set_cfg(cfg);

    rtd_data_t data = {};
    data.fault = Max31865Error::NoError;
    data.temperature = 92;

    // Maxes out duty in this case
    for (int i = 0; i < 10; i++) {
        boiler_temp_damper_process(i * 1e6, data);
        TEST_ASSERT_EQUAL(0, boiler_temp_damper_get_duty());
    }

    boiler_temp_damper_delete();
}

TEST_CASE("[boiler_temp_damper:test_persit_stats]", "Test that stats are persisted ok") {
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    boiler_temp_damper_init(g_event_loop);
    boiler_temp_damper_reset_stats();

    // Cause temp read error
    rtd_data_t data;
    data.fault = Max31865Error::RefHigh;
    boiler_temp_damper_process(1e6, data);
    TEST_ASSERT_EQUAL(1, boiler_temp_damper_get_status().brew_temp_read_error_count);

    // but it did not persist just yet
    boiler_temp_damper_delete();
    boiler_temp_damper_init(g_event_loop);
    TEST_ASSERT_EQUAL(0, boiler_temp_damper_get_status().brew_temp_read_error_count);

    // Do it again, issue TICK, and it will take straight away
    boiler_temp_damper_process(1.3e6, data);
    TEST_ASSERT_EQUAL(1, boiler_temp_damper_get_status().brew_temp_read_error_count);

    boiler_temp_damper_status_t stats;
    nvs_handle my_handle;
    uint32_t defaultVal = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_BOILER_DAMPER_STATS_STORE, NVS_READWRITE, &my_handle));

    // First tick saves straight away (zero last save time)
    uint64_t time_us = 1e6;
    ESP_ERROR_CHECK(esp_event_post_to(g_event_loop, MACHINE_EVENTS, TICK, &time_us, sizeof(uint64_t), portMAX_DELAY));
    vTaskDelay(pdMS_TO_TICKS(100));
    nvram_store_get_u32(my_handle, KEY_BOILER_DAMPER_STATS_TEMP_ERROR, (uint32_t *) &stats.brew_temp_read_error_count,
                        (void *) &defaultVal);
    TEST_ASSERT_EQUAL(1, stats.brew_temp_read_error_count);

    // Now do it again, it takes 5s to register
    boiler_temp_damper_process(1e6, data);

    for(int i=0; i<7; i++) {
        time_us += 1e6;  // 1s
        ESP_ERROR_CHECK(esp_event_post_to(g_event_loop, MACHINE_EVENTS, TICK, (void*)&time_us, sizeof(uint64_t), portMAX_DELAY));

        vTaskDelay(2);
        nvram_store_get_u32(my_handle, KEY_BOILER_DAMPER_STATS_TEMP_ERROR, (uint32_t *) &stats.brew_temp_read_error_count,
                            (void *) &defaultVal);

        if ( i >= 4) {
            TEST_ASSERT_EQUAL(2, stats.brew_temp_read_error_count);
        } else {
            TEST_ASSERT_EQUAL(1, stats.brew_temp_read_error_count);
        }
    }

    nvs_close(my_handle);
    boiler_temp_damper_delete();
}

TEST_CASE("[boiler_temp_damper:test_backoff_after_brew]", "Test damper off when brew started") {
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    boiler_temp_damper_init(g_event_loop);
    boiler_temp_damper_reset_cfg();

    // Turn off restart on RTD error
    auto cfg = boiler_temp_damper_get_cfg();
    cfg.enabled = true;
    cfg.pid.over_setpoint_perc = 0;
    cfg.pid.active_setpoint = 0;
    cfg.pid.setpoints[0] = 50;
    cfg.pid.I_reset_temp = 1;
    cfg.reset_time_sec = 10;
    boiler_temp_damper_set_cfg(cfg);

    rtd_data_t data = {};
    data.fault = Max31865Error::NoError;
    data.temperature = 92;

    uint64_t time_us = 0;
    // Maxes out duty in this case
    for (int i = 0; i < 10; i++) {
        time_us = i * 1e6;
        boiler_temp_damper_process(time_us, data);
        TEST_ASSERT_EQUAL(0, boiler_temp_damper_get_duty());
    }

    // Start brew, goes to zero immediately
    ESP_ERROR_CHECK(esp_event_post_to(g_event_loop, MACHINE_EVENTS, BREW_STARTED, (void *) &time_us, sizeof(uint64_t),
                                      portMAX_DELAY));
    vTaskDelay(10);

    for (int i = 0; i < 9; i++) {
        time_us += 1e6;
        boiler_temp_damper_process(time_us, data);
        TEST_ASSERT_EQUAL(cfg.max_damping_perc, boiler_temp_damper_get_duty());
    }

    // Ok, so now fast forward and we are back in business
    time_us += 1e6;
    boiler_temp_damper_process(time_us, data);
    TEST_ASSERT_EQUAL(0, boiler_temp_damper_get_duty());


    boiler_temp_damper_reset_cfg();
    boiler_temp_damper_delete();
}
