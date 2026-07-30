#include <src/thing_info.h>
#include <_generated/version.h>
#include <src/homekit/homekit.h>
#include <freertos/task.h>
#include <src/events.h>
#include <esp_log.h>
#include <ctime>
#include <hw_config.h>
#include <esp_timer.h>
#include <sys/param.h>
#include "iot.h"
#include "controller.h"
#include "rtds.h"
#include "boiler_temp.h"
#include "power.h"
#include "common/identity.h"
#include "app_metrics.h"
#include "brew_temp.h"
#include "boiler_refill.h"
#include "schedules.h"

#define TAG "iot"

#define IOT_LOOP_PERIOD             1000

static bool _go = true;

void iot_process_events() {
  while (_go) {
    time_t time_since_boot_millis = esp_timer_get_time() / 1000;

    // Approximately every second...
    time_t now = esp_timer_get_time() / 1000;
    if (IOT_LOOP_PERIOD > (now - time_since_boot_millis)) {
      vTaskDelay((IOT_LOOP_PERIOD - (now - time_since_boot_millis)) / portTICK_PERIOD_MS);
    }
  }
}

void iot_init() {
  homekit_init();
}
