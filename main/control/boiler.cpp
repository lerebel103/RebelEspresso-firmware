#include <cstdint>
#include <hw/rtds.h>
#include <esp_log.h>
#include <hw/r1.0/hw_config.h>
#include "boiler.h"

#define TAG "Boiler"

struct boiler_cfg_t {
    uint8_t mains_hz = 50;
};

static boiler_cfg_t s_cfg;

// Mapping of duty by index to [n, T] where n is the number of
// power cycles (1 sinusoid) and T the total number of cycles.
// Therefore duty = n / T
static uint16_t s_duty_map[][2] = {
        {3,  300},    // 1%
        {3,  150},    // 2%
        {3,  100},    // 3%
        {3,  75},    // 4%
        {3,  60},    // 5%
        {3,  50},    // 6%
        {7,  100},    // 7%
        {4,  50},    // 8%
        {9,  100},    // 9%
        {3,  30},    // 10%
        {11, 100},    // 11%
        {3,  25},    // 12%
        {13, 100},    // 13%
        {7,  50},    // 14%
        {3,  20},    // 15%
        {4,  25},    // 16%
        {17, 100},    // 17%
        {9,  50},    // 18%
        {19, 100},    // 19%
        {3,  15},    // 20%
        {21, 100},    // 21%
        {11, 50},    // 22%
        {23, 100},    // 23%
        {6,  25},    // 24%
        {3,  12},    // 25%
        {13, 50},    // 26%
        {27, 100},    // 27%
        {7,  25},    // 28%
        {29, 100},    // 28%
        {3,  10},    // 30%
        {31, 100},    // 31%
        {8,  25},    // 32%
        {33, 100},    // 33%
        {17, 50},    // 34%
        {7,  20},    // 35%
        {9,  25},    // 36%
        {37, 100},    // 37%
        {19, 50},    // 38%
        {39, 100},    // 39%
        {4,  10},    // 40%
        {41, 100},    // 41%
        {21, 50},    // 42%
        {43, 100},    // 43%
        {11, 25},    // 44%
        {9,  20},    // 45%
        {23, 50},    // 46%
        {47, 100},    // 47%
        {12, 25},    // 48%
        {49, 100},    // 49%
        {3,  6},    // 50%
        {51, 100},    // 51%
        {13, 25},    // 52%
        {53, 100},    // 53%
        {27, 50},    // 54%
        {11, 20},    // 55%
        {14, 25},    // 56%
        {57, 100},    // 56%
        {29, 50},    // 57%
        {59, 100},    // 59%
        {3,  5},    // 60%
        {61, 100},    // 61%
        {31, 50},    // 62%
        {63, 100},    // 63%
        {16, 25},    // 64%
        {13, 20},    // 65%
        {33, 50},    // 66%
        {67, 100},    // 67%
        {17, 25},    // 68%
        {69, 100},    // 69%
        {7,  10},    // 70%
        {71, 100},    // 71%
        {18, 25},    // 72%
        {73, 100},    // 73%
        {37, 50},    // 74%
        {3,  4},    // 75%
        {19, 25},    // 76%
        {77, 100},    // 77%
        {39, 50},    // 78%
        {79, 100},    // 79%
        {4,  5},    // 80%
        {81, 100},    // 81%
        {41, 50},    // 82%
        {83, 100},    // 83%
        {21, 25},    // 84%
        {17, 20},    // 85%
        {43, 50},    // 86%
        {87, 100},    // 87%
        {22, 25},    // 88%
        {89, 100},    // 89%
        {9,  10},    // 90%
        {91, 100},    // 91%
        {23, 25},    // 92%
        {93, 100},    // 93%
        {47, 50},    // 94%
        {19, 20},    // 95%
        {24, 25},    // 96%
        {97, 100},    // 97%
        {49, 50},    // 98%
        {99, 100},    // 99%
        {3,  3},    // 100%
};

/**
 * Converts a duty value ranging from [0, 100] to a period and pulse width in milliseconds
 *
 * @param duty integral value in [0, 100] range
 * @param period_ms returned period in ms for variable time base
 * @param pulse_ms width of the pulse to generate to acehive duty
 */
static void _duty_to_time_base(uint8_t duty, uint16_t &period_ms, uint16_t &pulse_ms) {
    if (duty > 100) {
        duty = 100;
    }

    period_ms = 1000 * s_duty_map[duty][0] / s_cfg.mains_hz;
    pulse_ms = 1000 * s_duty_map[duty][1] / s_cfg.mains_hz;
}

void boiler_tick(uint64_t time_us, const rtd_data_t& data) {
    if (data.fault == Max31865Error::NoError) {
        // Good to go
        ESP_LOGI(TAG, "Boiler temp=%f", data.temperature);

    } else {
        ESP_LOGE(TAG, "Boiler sensor error %s", Max31865::errorToString(data.fault));
    }
}

void boiler_init() {

}
