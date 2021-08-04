#pragma once


#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event_base.h>

void process_loop_init(esp_event_loop_handle_t event_loop);

