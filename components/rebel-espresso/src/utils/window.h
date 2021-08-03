#pragma once


extern "C" {
#include <cstdint>
#include <sys/queue.h>
}

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "rtds.h"

struct window_data_t {
    double mean;
    double min;
    double max;

    double error_integral;
    double derivative;
    uint16_t count;
};

struct window_entry_t {
    uint64_t time_us;
    uint64_t sequence;
    rtd_data_t data;
    float setpoint;
    STAILQ_ENTRY(window_entry_t) entries;
};

struct window_handle_t {
    STAILQ_HEAD(window, window_entry_t) queue;
};

void window_init(window_handle_t* handle);
void window_reset(window_handle_t* handle);

void window_accumulate(
        window_handle_t* handle,
        uint64_t tick,
        const rtd_data_t* result,
        float setpoint,
        uint16_t window_size_ms);

void window_data(window_handle_t* window, window_data_t* data);
