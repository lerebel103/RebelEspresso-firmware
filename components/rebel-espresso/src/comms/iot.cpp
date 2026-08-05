#include <src/device/thing_info.h>
#include <_generated/version.h>
#include <src/comms/homekit/homekit.h>
#include <freertos/task.h>
#include <src/events.h>
#include <esp_log.h>
#include <ctime>
#include <hw_config.h>
#include <esp_timer.h>
#include <esp_ota_ops.h>
#include <sys/param.h>
#include "iot.h"
#include "controller.h"
#include "rtds.h"
#include "boiler_temp.h"
#include "power.h"
#include "common/identity.h"
#include "common/events_common.h"
#include "app_metrics.h"
#include "brew_temp.h"
#include "boiler_refill.h"
#include "schedules.h"
#include "webserver/web_server.h"
#include "mqtt/mqtt_ha.h"

#define TAG "iot"

#define IOT_LOOP_PERIOD 1000

static bool _go = true;
static bool _web_server_started = false;
static bool _web_server_failed = false;
static bool _rollback_validated = false;

/**
 * OTA rollback self-test: mark the firmware as valid once WiFi is connected
 * and the web server is running. If neither happens, the firmware stays
 * unvalidated and the bootloader will roll back on the next crash/reboot.
 */
static void _check_rollback_validation() {
  if (_rollback_validated) {
    return;
  }

  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) != ESP_OK) {
    // Not an OTA partition (e.g., factory) — nothing to validate
    _rollback_validated = true;
    return;
  }

  if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
    // We booted from a new OTA image that hasn't been validated yet.
    // Only mark valid if WiFi is connected AND web server is running.
    EventBits_t bits = xEventGroupGetBits(status_event_group);
    if (bits & WIFI_CONNECTED_BIT) {
      esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
      if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA self-test passed — firmware marked valid");
        _rollback_validated = true;
      } else {
        ESP_LOGE(TAG, "Failed to mark firmware valid: %s", esp_err_to_name(err));
      }
    }
  } else {
    // Already validated or not pending
    _rollback_validated = true;
  }
}

void iot_process_events() {
  while (_go) {
    time_t time_since_boot_millis = esp_timer_get_time() / 1000;

    // Start web server once WiFi is connected OR AP mode is active
    if (!_web_server_started && !_web_server_failed) {
      EventBits_t bits = xEventGroupGetBits(status_event_group);
      if ((bits & WIFI_CONNECTED_BIT) || (bits & WIFI_AP_ACTIVE_BIT)) {
        if (web_server_start() == ESP_OK) {
          _web_server_started = true;
          ESP_LOGI(TAG, "Web server started (WiFi %s)", (bits & WIFI_CONNECTED_BIT) ? "STA connected" : "AP mode");
        } else {
          _web_server_failed = true;
          ESP_LOGE(TAG, "Web server failed to start — will not retry");
        }
      }
    }

    // OTA rollback self-test
    _check_rollback_validation();

    // MQTT / Home Assistant — only attempt once STA WiFi is connected.
    if (xEventGroupGetBits(status_event_group) & WIFI_CONNECTED_BIT) {
      mqtt_ha_service();
    }

    // Approximately every second...
    time_t now = esp_timer_get_time() / 1000;
    if (IOT_LOOP_PERIOD > (now - time_since_boot_millis)) {
      vTaskDelay((IOT_LOOP_PERIOD - (now - time_since_boot_millis)) / portTICK_PERIOD_MS);
    }
  }
}

void iot_init() {
  homekit_init();
  mqtt_ha_init();
}
