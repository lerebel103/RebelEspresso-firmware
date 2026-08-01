#pragma once

#include "esp_event_base.h"

/* Network event group bit definitions */
#define WIFI_CONNECTED_BIT (1 << 1)
#define WIFI_AP_ACTIVE_BIT (1 << 2)
#define SNTP_TIME_SYNCED_BIT (1 << 3)
#define MAX_ESP32_CONNECTIVITY_EVENTS_BIT SNTP_TIME_SYNCED_BIT
