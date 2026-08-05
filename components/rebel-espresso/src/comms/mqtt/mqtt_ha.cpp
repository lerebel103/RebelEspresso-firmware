#include "mqtt_ha.h"
#include "mqtt_config.h"

#include <mqtt_client.h> // esp-mqtt (ESP-IDF)
#include <esp_log.h>
#include <cstdio>
#include "common/identity.h"

#define TAG "mqtt_ha"

static esp_mqtt_client_handle_t s_client = nullptr;
static bool s_started = false;
static char s_base[80];
static char s_avail_topic[96];

static void _resolve_topics() {
  const mqtt_cfg_t& cfg = mqtt_config_get();
  if (cfg.base_topic[0] != '\0') {
    snprintf(s_base, sizeof(s_base), "%s", cfg.base_topic);
  } else {
    snprintf(s_base, sizeof(s_base), "rebelespresso/%s", identity_thing_id());
  }
  snprintf(s_avail_topic, sizeof(s_avail_topic), "%s/availability", s_base);
}

static void _event_handler([[maybe_unused]] void *args, [[maybe_unused]] esp_event_base_t base, int32_t id,
                           [[maybe_unused]] void *data) {
  switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(TAG, "Connected — publishing availability");
      esp_mqtt_client_publish(s_client, s_avail_topic, "online", 0, 1, 1);
      break;
    case MQTT_EVENT_DISCONNECTED:
      ESP_LOGW(TAG, "Disconnected");
      break;
    default:
      break;
  }
}

void mqtt_ha_init() {
  mqtt_config_load();
}

void mqtt_ha_service() {
  const mqtt_cfg_t& cfg = mqtt_config_get();

  if (!cfg.enabled) {
    if (s_started) {
      mqtt_ha_stop();
    }
    return;
  }

  if (s_started || cfg.broker_uri[0] == '\0') {
    return; // already running (esp-mqtt owns reconnect) or nothing to connect to
  }

  _resolve_topics();

  esp_mqtt_client_config_t mcfg = {};
  mcfg.broker.address.uri = cfg.broker_uri;
  if (cfg.username[0] != '\0') {
    mcfg.credentials.username = cfg.username;
  }
  if (cfg.password[0] != '\0') {
    mcfg.credentials.authentication.password = cfg.password;
  }
  if (cfg.client_id[0] != '\0') {
    mcfg.credentials.client_id = cfg.client_id;
  }
  mcfg.session.last_will.topic = s_avail_topic;
  mcfg.session.last_will.msg = "offline";
  mcfg.session.last_will.qos = 1;
  mcfg.session.last_will.retain = 1;

  s_client = esp_mqtt_client_init(&mcfg);
  if (s_client == nullptr) {
    ESP_LOGE(TAG, "client init failed");
    return;
  }

  esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, _event_handler, nullptr);
  if (esp_mqtt_client_start(s_client) == ESP_OK) {
    s_started = true;
    ESP_LOGI(TAG, "Started (%s)", cfg.broker_uri);
  } else {
    ESP_LOGE(TAG, "client start failed");
    esp_mqtt_client_destroy(s_client);
    s_client = nullptr;
  }
}

void mqtt_ha_stop() {
  if (s_client != nullptr) {
    esp_mqtt_client_publish(s_client, s_avail_topic, "offline", 0, 1, 1);
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = nullptr;
  }
  s_started = false;
}
