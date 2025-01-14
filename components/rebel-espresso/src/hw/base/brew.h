  #pragma once

#include <esp_event_base.h>

struct brew_status_t {
    uint32_t brew_count;
    uint32_t descale_count;
    time_t last_descale_time;
};

void brew_init();

void brew_update(uint64_t  time_us);

brew_status_t brew_get_status();
