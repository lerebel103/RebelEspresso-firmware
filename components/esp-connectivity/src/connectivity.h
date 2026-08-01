#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

void connectivity_init(EventGroupHandle_t net_group);
