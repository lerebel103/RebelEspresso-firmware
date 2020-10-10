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

TEST_CASE( "[boiler_temp]", "Ensure STANDBY cuts off power to SSR") {
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

TEST_CASE( "[boiler_temp]", "Ensure tick cuts power when conditions not met") {
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
        boiler_temp_tick(esp_timer_get_time(), data);

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
        boiler_temp_tick(esp_timer_get_time(), data);

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
        boiler_temp_tick(esp_timer_get_time(), data);

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
        boiler_temp_tick(esp_timer_get_time(), data);

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
        boiler_temp_tick(esp_timer_get_time(), data);

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
        boiler_temp_tick(esp_timer_get_time(), data);

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
        boiler_temp_tick(esp_timer_get_time(), data);

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
        boiler_temp_tick(esp_timer_get_time(), data);

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
        boiler_temp_tick(esp_timer_get_time(), data);

        // Now SSR must be powered off.
        TEST_ASSERT_EQUAL(0, boiler_temp_get_duty());
        state = (GPIO_REG_READ(GPIO_OUT_REG) >> BOILER_SSR_PIN) & 1U;
        TEST_ASSERT_FALSE(state);
    }

    boiler_temp_delete();
}

