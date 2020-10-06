#include <FreeRTOS.h>
#include <task.h>
#include <unity.h>

#include <esp_log.h>
#include <driver/rmt.h>

#include "control/boiler.h"
#include "control/rmt_duty_map.h"

extern "C" void boiler_set_duty(uint8_t duty);
extern "C" uint8_t boiler_get_duty();

TEST_CASE( "[boiler]", "Ensure enable/disable cuts off power to SSR") {
    boiler_init();

    // Not enabled by default
    TEST_ASSERT_EQUAL(0, boiler_get_duty());
    TEST_ASSERT_FALSE(boiler_is_enabled());

    boiler_enable(true);
    TEST_ASSERT_TRUE(boiler_is_enabled());

    // Fake 100% duty to turn on SSR
    uint8_t duty = 100;
    boiler_set_duty(duty);
    TEST_ASSERT_EQUAL(duty, boiler_get_duty());
    vTaskDelay(pdMS_TO_TICKS(200));

    // We can't access the gpio state as it is wired with RMT, for testing set to 1
    // and observe that it is dropped back to 0 forcefully when boiler is disabled
    // + duty is set to zero
    gpio_set_level(BOILER_SSR_PIN, 1);
    int state = (GPIO_REG_READ(GPIO_OUT_REG)  >> BOILER_SSR_PIN) & 1U;
    TEST_ASSERT_TRUE(state);

    // Check that our SSR GPIO is now latched on
    boiler_enable(false);
    vTaskDelay(pdMS_TO_TICKS(200));
    TEST_ASSERT_FALSE(boiler_is_enabled());
    TEST_ASSERT_EQUAL(0, boiler_get_duty());

    // Now SSR must be powered off.
    state = (GPIO_REG_READ(GPIO_OUT_REG)  >> BOILER_SSR_PIN) & 1U;
    TEST_ASSERT_FALSE(state);

    boiler_delete();
}


