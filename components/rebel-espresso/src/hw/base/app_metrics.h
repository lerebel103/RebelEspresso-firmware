#pragma once

#include <cstdint>
#include <ctime>

struct device_metrics_t {
  uint32_t boot_count;
  uint32_t crash_count;
  uint32_t last_crash_reason;
};


void app_metrics_send(time_t now, char *buffer, size_t max_len);

bool app_metrics_update_required(int interval_sec);

void app_metrics_reset_update();

device_metrics_t app_metrics_get();

void app_metrics_init();

