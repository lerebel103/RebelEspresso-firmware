#pragma once


#include <time.h>
#include <stdint.h>
#include <esp_err.h>
#include "version.h"

struct state_t {
    int wifi_rssi = 0;
    char wifi_bssid[32] = {0};
    int wifi_primary_channel = 0;
    uint16_t wifi_join_duration = 0;
};


void state_print_system_info();
void state_print_memory_info();

void state_send(time_t timestamp);
state_t& state_get();

esp_err_t state_init();


