#include <FreeRTOS.h>
#include <task.h>
#include <unity.h>

#include <esp_log.h>
#include "control/rmt_duty_map.h"

extern "C" void boiler_set_duty(uint8_t duty);
extern "C" uint8_t boiler_get_duty();

TEST_CASE( "[rmt_duty_map]", "Ensure out-of-bound duty is capped") {
    const rmt_pulse_t* pulses_0 = rmt_duty_get_pulses(0, MAINS_50HZ);
    const rmt_pulse_t* pulses_100 = rmt_duty_get_pulses(100, MAINS_50HZ);

    TEST_ASSERT_NOT_NULL(pulses_0);
    TEST_ASSERT_NOT_NULL(pulses_100);
    TEST_ASSERT_FALSE(pulses_0 == pulses_100);

    TEST_ASSERT_TRUE(pulses_0 == rmt_duty_get_pulses(0, MAINS_50HZ));
    TEST_ASSERT_TRUE(pulses_0 == rmt_duty_get_pulses(-1, MAINS_50HZ));
    TEST_ASSERT_TRUE(pulses_0 == rmt_duty_get_pulses(-100, MAINS_50HZ));

    TEST_ASSERT_TRUE(pulses_100 == rmt_duty_get_pulses(100, MAINS_50HZ));
    TEST_ASSERT_TRUE(pulses_100 == rmt_duty_get_pulses(101, MAINS_50HZ));
    TEST_ASSERT_TRUE(pulses_100 == rmt_duty_get_pulses(200, MAINS_50HZ));
}

static void _test_duty(int mains_hz) {
    for(int duty=0; duty <= 100; duty++) {
        const rmt_pulse_t* pulses = rmt_duty_get_pulses(duty, mains_hz);
        TEST_ASSERT_NOT_NULL(pulses);
        TEST_ASSERT_GREATER_THAN(0, pulses->num_items);

        // Sum up on and off pulses duration and ensure it matches the duty desired
        int on_duration = 0;
        int off_duration = 0;
        for (int i=0; i<pulses->num_items-1; i++) {
            if (pulses->items[i].level0) {
                on_duration += pulses->items[i].duration0;
            } else {
                off_duration += pulses->items[i].duration0;
            }

            if (pulses->items[i].level1) {
                on_duration += pulses->items[i].duration1;
            } else {
                off_duration += pulses->items[i].duration1;
            }
        }
        float effective_duty = 100 * float(on_duration) / float(on_duration + off_duration);
        TEST_ASSERT_LESS_OR_EQUAL(fabs(duty - effective_duty), 0.001);

        // Last element must always be a terminator for RMT
        const rmt_item32_s& last_item = pulses->items[pulses->num_items-1];
        TEST_ASSERT_EQUAL(0, last_item.duration0);
        TEST_ASSERT_EQUAL(1, last_item.level0);
        TEST_ASSERT_EQUAL(0, last_item.duration1);
        TEST_ASSERT_EQUAL(0, last_item.level1);
    }
}

TEST_CASE( "[rmt_duty_map]", "Validate duty map 50Hz")
{
    int mains_hz = MAINS_50HZ;
    _test_duty(mains_hz);
}

TEST_CASE( "[rmt_duty_map]", "Validate duty map 60Hz")
{
    int mains_hz = MAINS_60HZ;
    _test_duty(mains_hz);
}
