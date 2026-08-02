#pragma once

#include <esp_bit_defs.h>
#include <freertos/event_groups.h>

#include <ctime>

#define DEFAULT_SYSTEM_TZ "AWST-8"

struct sntp_metrics_t {
  uint32_t sync_duration_ms;
  time_t last_sync_time;
};

void sntp_sync_init(EventGroupHandle_t networkEventGroup, const char *primary_server = nullptr);

void sntp_set_system_tz(const char *tz);
const char *sntp_get_system_tz();

sntp_metrics_t sntp_sync_get_metrics();
