#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unity.h>

#include <esp_log.h>
#include <driver/rmt.h>
#include <esp_event.h>
#include <src/sys/nvram_store.h>

#include "control/boiler_temp.h"
#include "control/rmt_duty_map.h"
#include "events.h"

extern "C" void boiler_temp_set_duty(uint8_t duty);

extern esp_event_loop_handle_t g_event_loop;

TEST_CASE("[boiler_temp:test_standby_ssr_off]", "Ensure STANDBY cuts off power to SSR") {
    boiler_temp_init(g_event_loop);

    // Turn off restart on RTD error
    auto cfg = boiler_temp_get_cfg();
    cfg.temp_error_restart_time_sec = 0;
    boiler_temp_set_cfg(cfg);

    // Not enabled by default, zero duty
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());

    // Fake 100% duty to turn on SSR
    uint8_t duty = 100;
    boiler_temp_set_duty(duty);
    TEST_ASSERT_EQUAL(duty, boiler_temp_get_duty());
    vTaskDelay(pdMS_TO_TICKS(200));

    // We can't access the gpio state as it is wired with RMT, for testing set to 1
    // and observe that it is dropped back to 0 forcefully when boiler_temp is disabled
    // + duty is set to zero
    gpio_set_level(BOILER_SSR_PIN, 1);
    int state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
    TEST_ASSERT_TRUE(state);

    // Generate a standby event, we have zero power to SSR
    ESP_ERROR_CHECK(esp_event_post_to(g_event_loop, MACHINE_EVENTS, POWER_STANDBY, nullptr, 0, portMAX_DELAY));

    // Now SSR must be powered off.
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
    state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
    TEST_ASSERT_FALSE(state);

    boiler_temp_delete();
}

TEST_CASE("[boiler_temp:test_error_conditions]", "Ensure process cuts power when conditions not met") {
    boiler_temp_init(g_event_loop);
    boiler_temp_reset_stats();

    // Turn off restart on RTD error
    auto cfg = boiler_temp_get_cfg();
    cfg.temp_error_restart_time_sec = 0;
    boiler_temp_set_cfg(cfg);

    // Not enabled by default, zero duty
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());


    // We can't access the gpio state as it is wired with RMT, for testing set to 1
    // and observe that it is dropped back to 0 forcefully when boiler_temp is disabled
    // + duty is set to zero
    boiler_temp_set_duty(100);
    gpio_set_level(BOILER_SSR_PIN, 1);
    int state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
    TEST_ASSERT_TRUE(state);

    for (int i = 0; i < 10; i++) {
        // --  Standby
        // Mark as standby mode and generate a tick process loop
        xEventGroupClearBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        rtd_data_t data;
        data.fault = Max31865Error::NoError;
        boiler_temp_process(esp_timer_get_time(), data);

        // Now SSR must be powered off.
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
        TEST_ASSERT_FALSE(state);

        // --  Boiler level not good
        // Boiler empty but power active
        boiler_temp_set_duty(100);
        gpio_set_level(BOILER_SSR_PIN, 1);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupClearBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::NoError;
        boiler_temp_process(esp_timer_get_time(), data);

        // Now SSR must be powered off.
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
        TEST_ASSERT_FALSE(state);

        // --  RTD broken, High
        // Boiler empty but power active
        boiler_temp_set_duty(100);
        gpio_set_level(BOILER_SSR_PIN, 1);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RTDHigh;
        boiler_temp_process(esp_timer_get_time(), data);

        // Now SSR must be powered off.
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
        TEST_ASSERT_FALSE(state);

        // --  RTD broken, Low
        // Boiler empty but power active
        boiler_temp_set_duty(100);
        gpio_set_level(BOILER_SSR_PIN, 1);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RTDLow;
        boiler_temp_process(esp_timer_get_time(), data);

        // Now SSR must be powered off.
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
        TEST_ASSERT_FALSE(state);

        // --  RTD broken, RTDInLow
        // Boiler empty but power active
        boiler_temp_set_duty(100);
        gpio_set_level(BOILER_SSR_PIN, 1);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RTDInLow;
        boiler_temp_process(esp_timer_get_time(), data);

        // Now SSR must be powered off.
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
        TEST_ASSERT_FALSE(state);

        // --  RTD broken, RefHigh
        // Boiler empty but power active
        boiler_temp_set_duty(100);
        gpio_set_level(BOILER_SSR_PIN, 1);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RefHigh;
        boiler_temp_process(esp_timer_get_time(), data);

        // Now SSR must be powered off.
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
        TEST_ASSERT_FALSE(state);

        // --  RTD broken, RefLow
        // Boiler empty but power active
        boiler_temp_set_duty(100);
        gpio_set_level(BOILER_SSR_PIN, 1);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RefLow;
        boiler_temp_process(esp_timer_get_time(), data);

        // Now SSR must be powered off.
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
        TEST_ASSERT_FALSE(state);

        // --  RTD broken, Ref high
        // Boiler empty but power active
        boiler_temp_set_duty(100);
        gpio_set_level(BOILER_SSR_PIN, 1);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::RefHigh;
        boiler_temp_process(esp_timer_get_time(), data);

        // Now SSR must be powered off.
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
        TEST_ASSERT_FALSE(state);

        // --  RTD broken, voltage
        // Boiler empty but power active
        boiler_temp_set_duty(100);
        gpio_set_level(BOILER_SSR_PIN, 1);
        xEventGroupSetBits(status_event_group, POWER_ON_BIT);
        xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
        data.fault = Max31865Error::Voltage;
        boiler_temp_process(esp_timer_get_time(), data);

        // Now SSR must be powered off.
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
        TEST_ASSERT_FALSE(state);
    }

    // With above tests we expected this many temp read erros
    TEST_ASSERT_EQUAL(70, boiler_temp_get_status().temp_read_error_count);


    boiler_temp_delete();
}

TEST_CASE("[boiler_temp:test_update_config_from_json]", "Ensures partial json updates work") {
    boiler_temp_init(g_event_loop);
    boiler_temp_reset_cfg();
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "cfg." BOILER_CFG_JSON_KEY "pid.P", 4.1);
    cJSON_AddNumberToObject(root, "cfg." BOILER_CFG_JSON_KEY "pid.I", 0.5);
    cJSON_AddNumberToObject(root, "cfg." BOILER_CFG_JSON_KEY "pid.D", 60.4);
    cJSON_AddNumberToObject(root, "cfg." BOILER_CFG_JSON_KEY "pid.setpoint0", 99.9);
    cJSON_AddNumberToObject(root, "cfg." BOILER_CFG_JSON_KEY "temp_error_restart_time_sec", 1);

    boiler_temp_update_cfg(root);
    boiler_temp_cfg_t cfg = boiler_temp_get_cfg();
    TEST_ASSERT_EQUAL(4.1, cfg.pid.P);
    TEST_ASSERT_EQUAL(0.5, cfg.pid.I);
    TEST_ASSERT_EQUAL(60.4, cfg.pid.D);
    TEST_ASSERT_EQUAL(99.9, cfg.pid.setpoints[0]);
    TEST_ASSERT_EQUAL(1, cfg.temp_error_restart_time_sec);

    cJSON_Delete(root);
    boiler_temp_delete();
}


TEST_CASE("[boiler_temp:test_nvs_load_save]", "Test load/save config works") {
    boiler_temp_init(g_event_loop);

    boiler_temp_cfg_t new_cfg;
    new_cfg.pid.P = 5.5;
    new_cfg.pid.I = 5.6;
    new_cfg.pid.D = 5.7;
    new_cfg.pid.I_reset_temp = 5.8;
    new_cfg.pid.I_reset_sec = 6;
    new_cfg.pid.setpoints[0] = 51.2;
    new_cfg.pid.over_setpoint_perc = 13.1;
    new_cfg.pid.min_duty_band = 6.1;
    new_cfg.mains_hz = 60;
    new_cfg.temp_error_restart_time_sec = 58;

    // Apply
    boiler_temp_set_cfg(new_cfg);

    // Now kill this instance and reload it
    boiler_temp_delete();
    boiler_temp_init(g_event_loop);

    // Get config back, it must be identical
    auto cfg = boiler_temp_get_cfg();

    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.P, cfg.pid.P);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.I, cfg.pid.I);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.D, cfg.pid.D);
    TEST_ASSERT_EQUAL(new_cfg.pid.I_reset_sec, cfg.pid.I_reset_sec);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.I_reset_temp, cfg.pid.I_reset_temp);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.setpoints[0], cfg.pid.setpoints[0]);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.over_setpoint_perc, cfg.pid.over_setpoint_perc);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.min_duty_band, cfg.pid.min_duty_band);
    TEST_ASSERT_EQUAL(new_cfg.mains_hz, cfg.mains_hz);
    TEST_ASSERT_EQUAL(new_cfg.temp_error_restart_time_sec, cfg.temp_error_restart_time_sec);

    boiler_temp_delete();
}

TEST_CASE("[boiler_temp:test_nvs_reset_default]", "Test resetting NVS to defaults") {
    // reset
    boiler_temp_reset_cfg();
    boiler_temp_init(g_event_loop);

    // Formulate a JSON object, set it and make sure we get the right answer
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.P", 4.1);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.I", 0.5);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.D", 60.4);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.I_reset_sec", 16);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.I_reset_temp", 14);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.setpoint0", 125);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.setpoint1", 139);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.over_setpoint_perc", 25);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.min_duty_band", 8);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "mains_hz", 60);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "temp_error_restart_time_sec", 59);
    char *json = cJSON_PrintUnformatted(root);


    boiler_temp_cfg_t cfg = {};

    // Apply and get back to compare
    cfg.from_json(root);
    TEST_ASSERT_EQUAL(4.1, cfg.pid.P);
    TEST_ASSERT_EQUAL(0.5, cfg.pid.I);
    TEST_ASSERT_EQUAL(60.4, cfg.pid.D);
    TEST_ASSERT_EQUAL(16, cfg.pid.I_reset_sec);
    TEST_ASSERT_EQUAL(14, cfg.pid.I_reset_temp);
    TEST_ASSERT_EQUAL(125, cfg.pid.setpoints[0]);
    TEST_ASSERT_EQUAL(139, cfg.pid.setpoints[1]);
    TEST_ASSERT_EQUAL(25, cfg.pid.over_setpoint_perc);
    TEST_ASSERT_EQUAL(8, cfg.pid.min_duty_band);
    TEST_ASSERT_EQUAL(60, cfg.mains_hz);
    TEST_ASSERT_EQUAL(59, cfg.temp_error_restart_time_sec);

    cJSON *new_cfg = cJSON_CreateObject();
    cfg.to_json(new_cfg, "");
    char *new_json = cJSON_PrintUnformatted(new_cfg);
    TEST_ASSERT_EQUAL_STRING(json, new_json);

    cJSON_free(json);
    cJSON_free(new_json);
    cJSON_Delete(root);
    cJSON_Delete(new_cfg);

    boiler_temp_delete();
}

TEST_CASE("[boiler_temp:test_boiler_setpoint_inc]", "Test increment boiler setpoint") {
    boiler_temp_init(g_event_loop);

    auto cfg = boiler_temp_get_cfg();
    double inc = -0.5;
    double expected = cfg.pid.setpoints[0];
    for (int i = 0; i < 10; i++) {
        expected += inc;
        TEST_ASSERT_EQUAL_DOUBLE(expected, boiler_setpoint_inc(inc));

        // Now this must be stored in nvram
        boiler_temp_delete();
        boiler_temp_init(g_event_loop);
        TEST_ASSERT_EQUAL_DOUBLE(expected, boiler_temp_get_cfg().pid.setpoints[0]);
    }

    // go out of bounds
    inc = 1000;
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_DOUBLE(SETPOINT0_MAX, boiler_setpoint_inc(inc));
        TEST_ASSERT_EQUAL_DOUBLE(SETPOINT0_MAX, boiler_temp_get_cfg().pid.setpoints[0]);
    }
    boiler_temp_delete();
    boiler_temp_init(g_event_loop);
    TEST_ASSERT_EQUAL_DOUBLE(SETPOINT0_MAX, boiler_temp_get_cfg().pid.setpoints[0]);

    inc = -1000;
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_DOUBLE(SETPOINT0_MIN, boiler_setpoint_inc(inc));
        TEST_ASSERT_EQUAL_DOUBLE(SETPOINT0_MIN, boiler_temp_get_cfg().pid.setpoints[0]);
    }
    boiler_temp_delete();
    boiler_temp_init(g_event_loop);
    TEST_ASSERT_EQUAL_DOUBLE(SETPOINT0_MIN, boiler_temp_get_cfg().pid.setpoints[0]);

    boiler_temp_delete();
}

TEST_CASE("[boiler_temp:test_boiler_duty_steady]", "Test duty when steady temp fed") {
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    boiler_temp_init(g_event_loop);
    boiler_temp_reset_cfg();

    // Turn off restart on RTD error
    auto cfg = boiler_temp_get_cfg();
    cfg.temp_error_restart_time_sec = 0;
    cfg.pid.setpoints[0] = 120;
    boiler_temp_set_cfg(cfg);

    rtd_data_t data = {};
    data.fault = Max31865Error::NoError;
    data.temperature = 25;

    // Maxes out duty in this case
    for (int i = 0; i < 50; i++) {
        boiler_temp_process(i * 1e6, data);
        TEST_ASSERT_EQUAL(100, boiler_temp_get_duty());
    }

    boiler_temp_delete();
}

TEST_CASE("[boiler_temp:test_boiler_duty_ramp_up]", "Test duty when temp ramp up") {
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    boiler_temp_init(g_event_loop);
    boiler_temp_reset_cfg();

    boiler_temp_cfg_t cfg = boiler_temp_get_cfg();
    cfg.pid.P = 3;
    cfg.pid.I = 0.5;
    cfg.pid.D = 100;
    cfg.pid.setpoints[0] = 120;
    cfg.pid.over_setpoint_perc = 10;
    cfg.temp_error_restart_time_sec = 0;
    boiler_temp_set_cfg(cfg);

    rtd_data_t data;
    data.fault = Max31865Error::NoError;
    data.temperature = 25;

    double expectedDuties[] = {
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            100,
            99,
            96,
            93,
            90,
            87,
            84,
            81,
            78,
            75,
            72,
            69,
            66,
            63,
            60,
            57,
            54,
            51,
            48,
            45,
            42,
            39,
            36,
            33,
            30,
            27,
            24,
            21,
            18,
            15,
            12,
            8,
            5,
            4,
            0,
    };

    for (int i = 0; i < 150; i++) {
        data.temperature = 25 + i;
        boiler_temp_process(i * 1e6, data);

        // printf("%d,\r\n", boiler_temp_get_duty());
        // Cuts off with these settings
        if (data.temperature > 88) {
            TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        } else {
            TEST_ASSERT_EQUAL(expectedDuties[i], boiler_temp_get_duty());
        }
    }

    // Over temp got triggered this many times
    TEST_ASSERT_EQUAL(17, boiler_temp_get_status().temp_over_limit_count);

    boiler_temp_delete();
}

TEST_CASE("[boiler_temp:test_persit_stats]", "Test that stats are persisted ok") {
    xEventGroupSetBits(status_event_group, POWER_ON_BIT);
    xEventGroupSetBits(status_event_group, BOILER_LEVEL_OK_BIT);
    boiler_temp_init(g_event_loop);
    boiler_temp_reset_stats();

    // Cause temp read error
    rtd_data_t data;
    data.fault = Max31865Error::RefHigh;
    boiler_temp_process(1e6, data);
    TEST_ASSERT_EQUAL(1, boiler_temp_get_status().temp_read_error_count);

    // but it did not persist just yet
    boiler_temp_delete();
    boiler_temp_init(g_event_loop);
    TEST_ASSERT_EQUAL(0, boiler_temp_get_status().temp_read_error_count);

    // Do it again, issue TICK, and it will take straight away
    boiler_temp_process(1.3e6, data);
    TEST_ASSERT_EQUAL(1, boiler_temp_get_status().temp_read_error_count);

    boiler_temp_status_t stats;
    nvs_handle my_handle;
    uint32_t defaultVal = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_BOILER_STATS_STORE, NVS_READWRITE, &my_handle));

    // First tick saves straight away (zero last save time)
    uint64_t time_us = 1e6;
    ESP_ERROR_CHECK(esp_event_post_to(g_event_loop, MACHINE_EVENTS, TICK, &time_us, sizeof(uint64_t), portMAX_DELAY));
    vTaskDelay(pdMS_TO_TICKS(100));
    nvram_store_get_u32(my_handle, KEY_BOILER_STATS_TEMP_ERROR, (uint32_t *) &stats.temp_read_error_count,
                        (void *) &defaultVal);
    TEST_ASSERT_EQUAL(1, stats.temp_read_error_count);

    // Now do it again, it takes 5s to register
    boiler_temp_process(1e6, data);

    for (int i = 0; i < 7; i++) {
        time_us += 1e6;  // 1s
        ESP_ERROR_CHECK(esp_event_post_to(g_event_loop, MACHINE_EVENTS, TICK, (void *) &time_us, sizeof(uint64_t),
                                          portMAX_DELAY));

        vTaskDelay(2);
        nvram_store_get_u32(my_handle, KEY_BOILER_STATS_TEMP_ERROR, (uint32_t *) &stats.temp_read_error_count,
                            (void *) &defaultVal);

        if (i >= 4) {
            TEST_ASSERT_EQUAL(2, stats.temp_read_error_count);
        } else {
            TEST_ASSERT_EQUAL(1, stats.temp_read_error_count);
        }
    }

    nvs_close(my_handle);
    boiler_temp_delete();
}