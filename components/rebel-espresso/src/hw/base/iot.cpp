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
#include "core_mqtt_serializer.h"
#include "mqtt/mqtt_client.h"
#include "app_metrics.h"
#include "device_info.h"

#define TAG "iot"

#define CONTROL_LOOP_PERIOD         1000
#define IOT_SEND_INTERVAL_ACTIVE    1000
#define IOT_SEND_INTERVAL_INACTIVE  60000


static bool _go = true;

#define TOPIC_MAX_SIZE (128)
#define PAYLOAD_MAX_SIZE (2048)

static char info_topic[TOPIC_MAX_SIZE];
static char payload[PAYLOAD_MAX_SIZE];

static void _send_telemetry(time_t timestamp) {
  static const char *telemetry_format =
      R"({
      "status": {
        "timestamp": %)" PRIu64 R"(,
        "internal.temp.val": %.2f,
        "internal.temp.fault": %)" PRIu8 R"(,
        "boiler1.temp.val": %.2f,
        "boiler1.temp.fault": %)" PRIu8 R"(,
        "boiler1.setpoint": %.2f,
        "boiler1.heat_duty": %)" PRIu8 R"(,
        "brew.temp.val": %.2f,
        "brew.temp.fault": %)" PRIu8 R"(,
        "boiler2.temp.val": %.2f,
        "boiler2.temp.fault": %)" PRIu8 R"(
        }
      })";

  struct measure_t data_internal = {};
  rtds_get(&data_internal, RTD_INTERNAL_IDX);
  struct measure_t data_boiler1 = {};
  rtds_get(&data_boiler1, RTD_BREW_BOILER_IDX);
  struct measure_t data_brew = {};
  rtds_get(&data_brew, RTD_BREW_HEAD_IDX);
  struct measure_t data_boiler2 = {};
  rtds_get(&data_boiler2, RTD_STEAM_BOILER_IDX);

  size_t len = snprintf(payload, PAYLOAD_MAX_SIZE, telemetry_format,
                        timestamp,
                        (float)data_internal.value, data_internal.fault,
                        (float)data_boiler1.value, data_boiler1.fault,
                        (float)boiler_temp_get_current_setpoint(), (uint8_t)boiler_temp_get_duty(),
                        (float)data_brew.value, data_brew.fault,
                        (float)data_boiler2.value, data_boiler2.fault
  );

  MQTTPublishInfo_t publishInfo = {
      .qos = MQTTQoS1,
      .retain = false,
      .dup = false,
      .pTopicName = info_topic,
      .topicNameLength = (uint16_t) strlen(info_topic),
      .pPayload = payload,
      .payloadLength = len,
  };

  ESP_LOGD(TAG, "%.*s\n", len, payload);
  mqtt_client_publish(&publishInfo, CONFIG_MQTT_ACK_TIMEOUT_MS);
}



void iot_process_events() {
  time_t last_telemetry_update_tick = 0;
  while (_go) {
    time_t time_since_boot_millis = esp_timer_get_time() / 1000;
    time_t wall_clock_now = time(nullptr);
    auto send_interval = (power_is_active() ? IOT_SEND_INTERVAL_ACTIVE : IOT_SEND_INTERVAL_INACTIVE);

    // Send MQTT stuff as required
    if (xEventGroupGetBits(status_event_group) & CORE_MQTT_CLIENT_CONNECTED_BIT) {
      if (app_metrics_update_required(MAX(10, send_interval/1000))) {
        app_metrics_send(wall_clock_now, payload, PAYLOAD_MAX_SIZE);
      }
      if (device_info_update_required()) {
        device_info_send(payload, PAYLOAD_MAX_SIZE);
      }

      // Send telemetry
      if((time_since_boot_millis - last_telemetry_update_tick) > send_interval) {
        last_telemetry_update_tick = time_since_boot_millis;
        _send_telemetry(wall_clock_now);
      }
    }

    // Approximately every second...
    time_t now = esp_timer_get_time() / 1000;
    if (CONTROL_LOOP_PERIOD > (now - time_since_boot_millis)) {
      // Run event loop dispatch
      vTaskDelay((CONTROL_LOOP_PERIOD - (now - time_since_boot_millis)) / portTICK_PERIOD_MS);
    }
  }
}

void iot_init() {
  // Regular telemetry
  sprintf(info_topic, "%s/%s/telemetry/status", CMAKE_THING_TYPE, identity_thing_id());
  app_metrics_init();
  device_info_init();

  homekit_init();
}

