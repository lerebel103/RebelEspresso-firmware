#include <FreeRTOS.h>
#include <task.h>
#include <unity.h>

#include <esp_log.h>
#include <driver/rmt.h>
#include <esp_event.h>

#include "control/boiler_temp.h"
#include "control/rmt_duty_map.h"
#include "events.h"

extern "C" void boiler_temp_set_duty(uint8_t duty);
extern "C" uint8_t boiler_temp_get_duty();

extern esp_event_loop_handle_t g_event_loop;

TEST_CASE( "[boiler_temp:test_standby_ssr_off]", "Ensure STANDBY cuts off power to SSR") {
    boiler_temp_init(g_event_loop);

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
    int state = (GPIO_REG_READ(GPIO_OUT_REG)  >> BOILER_SSR_PIN) & 1U;
    TEST_ASSERT_TRUE(state);

    // Generate a standby event, we have zero power to SSR
    ESP_ERROR_CHECK(esp_event_post_to(g_event_loop, MACHINE_EVENTS, POWER_STANDBY, nullptr, 0, portMAX_DELAY));

    // Now SSR must be powered off.
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
    state = (GPIO_REG_READ(GPIO_OUT_REG)  >> BOILER_SSR_PIN) & 1U;
    TEST_ASSERT_FALSE(state);

    boiler_temp_delete();
}

TEST_CASE( "[boiler_temp:test_error_conditions]", "Ensure tick cuts power when conditions not met") {
    boiler_temp_init(g_event_loop);

    // Not enabled by default, zero duty
    TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());


    // We can't access the gpio state as it is wired with RMT, for testing set to 1
    // and observe that it is dropped back to 0 forcefully when boiler_temp is disabled
    // + duty is set to zero
    boiler_temp_set_duty(100);
    gpio_set_level(BOILER_SSR_PIN, 1);
    int state = (GPIO_REG_READ(GPIO_OUT_REG)  >> BOILER_SSR_PIN) & 1U;
    TEST_ASSERT_TRUE(state);

    for(int i=0; i<10; i++) {
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

    boiler_temp_delete();
}

TEST_CASE( "[boiler_temp:test_nvs_load_save]", "Test load/save config works") {
    boiler_temp_init(g_event_loop);

    boiler_temp_cfg_t new_cfg;
    new_cfg.pid.P = 5.5;
    new_cfg.pid.I = 5.6;
    new_cfg.pid.D = 5.7;
    new_cfg.pid.I_reset_temp = 5.8;
    new_cfg.pid.I_reset_sec = 6;
    new_cfg.pid.setpoint = 7.5;
    new_cfg.pid.over_setpoint_perc = 13.1;
    new_cfg.mains_hz = 60;

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
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.setpoint, cfg.pid.setpoint);
    TEST_ASSERT_EQUAL_DOUBLE(new_cfg.pid.over_setpoint_perc, cfg.pid.over_setpoint_perc);
    TEST_ASSERT_EQUAL(new_cfg.mains_hz, cfg.mains_hz);

    boiler_temp_delete();
}

TEST_CASE( "[boiler_temp:test_nvs_reset_default]", "Test resetting NVS to defaults") {
    // reset
    boiler_temp_reset_cfg();
    boiler_temp_init(g_event_loop);

    // Formulate a JSON object, set it and make sure we get the right answer
    cJSON* root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.P", 4.1);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.I", 0.5);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.D", 60.4);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.I_reset_sec", 16);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.I_reset_temp", 14);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.setpoint", 125);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "pid.over_setpoint_perc", 25);
    cJSON_AddNumberToObject(root, BOILER_CFG_JSON_KEY "mains_hz", 60);
    char* json = cJSON_PrintUnformatted(root);


    boiler_temp_cfg_t cfg = {};

    // Apply and get back to compare
    cfg.from_json(root);
    TEST_ASSERT_EQUAL(4.1, cfg.pid.P);
    TEST_ASSERT_EQUAL(0.5, cfg.pid.I);
    TEST_ASSERT_EQUAL(60.4, cfg.pid.D);
    TEST_ASSERT_EQUAL(16, cfg.pid.I_reset_sec);
    TEST_ASSERT_EQUAL(14, cfg.pid.I_reset_temp);
    TEST_ASSERT_EQUAL(125, cfg.pid.setpoint);
    TEST_ASSERT_EQUAL(25, cfg.pid.over_setpoint_perc);
    TEST_ASSERT_EQUAL(60, cfg.mains_hz);

    cJSON* new_cfg = cJSON_CreateObject();
    cfg.to_json(new_cfg, "");
    char* new_json = cJSON_PrintUnformatted(new_cfg);
    TEST_ASSERT_EQUAL_STRING(json, new_json);
    
    cJSON_free(json);
    cJSON_free(new_json);
    cJSON_Delete(root);
    cJSON_Delete(new_cfg);

    boiler_temp_delete();
}

