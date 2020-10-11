#include "window.h"
#include <esp_log.h>
#include <sys/param.h>

static const char *TAG = "window";

static uint64_t g_sequence = 0;


void window_init(window_handle_t *window) {
    ESP_LOGD(TAG, "Initialising new window");
    window->queue = STAILQ_HEAD_INITIALIZER(window->queue);

    g_sequence = 0;
    ESP_LOGD(TAG, "Queue initialised");
}

void window_reset(window_handle_t *window) {
    window_entry_t *item = NULL;
    STAILQ_FOREACH(item, &window->queue, entries) {
        free(item);
    }
    window_init(window);
}

void window_accumulate(
        window_handle_t *window,
        uint64_t time_us,
        const rtd_data_t *result,
        float setpoint,
        uint16_t window_size_ms) {

    // Make room and free old entries
    if(!STAILQ_EMPTY(&window->queue)) {
        window_entry_t *first = STAILQ_FIRST(&window->queue);
        while (first != NULL && (time_us - first->time_us) > window_size_ms * 1e3) {
            ESP_LOGD(TAG, "Dropping %d", (int)(first->time_us / 1e6));
            STAILQ_REMOVE(&window->queue, first, window_entry_t, entries);
            free(first);
            if (STAILQ_EMPTY(&window->queue)) {
                break;
            }
            first = STAILQ_FIRST(&window->queue);
        }
    }

    // Add to window
    window_entry_t *new_entry = (window_entry_t *) calloc(sizeof(window_entry_t), 1);
    new_entry->time_us = time_us;
    new_entry->sequence = ++g_sequence;
    new_entry->data = *result;
    new_entry->setpoint = setpoint;

    STAILQ_INSERT_TAIL(&window->queue, new_entry, entries);
}


void window_data(window_handle_t *window, window_data_t *data) {
    // Update data accumulations
    data->count = 0;
    data->mean = 0;
    data->max = INT32_MIN;
    data->min = INT32_MAX;

    data->error_integral = 0;
    window_entry_t *previous = NULL;
    window_entry_t *item = NULL;
    STAILQ_FOREACH(item, &window->queue, entries) {
        if (item && item->data.fault == Max31865Error::NoError) {
            data->count++;
            data->min = MIN(data->min, item->data.temperature);
            data->max = MAX(data->max, item->data.temperature);
            data->mean += item->data.temperature;

            if (previous != NULL) {
                double dt = (double)((item->time_us - previous->time_us)) / 1e6;
                data->error_integral += (item->setpoint - item->data.temperature) * dt;
            }
            previous = item;
        }
    }

    // Adjust mean and smoothed
    if (data->count != 0) {
        data->mean = data->mean / data->count;
    }
    ESP_LOGD(TAG, "Windowed data mean=%f, min=%f, max=%f, error_integral=%f",
             data->mean, data->min, data->max, data->error_integral);
}

