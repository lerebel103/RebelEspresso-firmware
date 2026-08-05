#include "mqtt_ha.h"
#include "mqtt_config.h"
#include "mqtt_discovery.h"

#include <mqtt_client.h> // esp-mqtt (ESP-IDF)
#include <esp_log.h>
#include <esp_app_desc.h>
#include <esp_timer.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include "common/identity.h"
#include "process_image.h"
#include "boiler_temp.h"
#include "brew_temp.h"
#include "boiler_refill.h"
#include "boiler_refill_states.h"
#include "rtds.h"
#include "power.h"
#include <hw_config.h>

#define TAG "mqtt_ha"

static esp_mqtt_client_handle_t s_client = nullptr;
static bool s_started = false;
static bool s_connected = false;
static char s_base[80];
static char s_avail_topic[96];
static char s_disc_prefix[32];
static char s_node_id[80];
static char s_dev_name[48];
static char s_model[24];
static int64_t s_last_state_us = 0;

// Sanitise an id to [A-Za-z0-9_] for use in topics / unique_ids.
static void _sanitise(char *dst, size_t size, const char *src) {
  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 1 < size; i++) {
    char c = src[i];
    dst[j++] = (isalnum((unsigned char)c) ? c : '_');
  }
  dst[j] = '\0';
}

static void _resolve_topics() {
  const mqtt_cfg_t& cfg = mqtt_config_get();
  if (cfg.base_topic[0] != '\0') {
    snprintf(s_base, sizeof(s_base), "%s", cfg.base_topic);
  } else {
    snprintf(s_base, sizeof(s_base), "rebelespresso/%s", identity_thing_id());
  }
  snprintf(s_avail_topic, sizeof(s_avail_topic), "%s/availability", s_base);

  snprintf(s_disc_prefix, sizeof(s_disc_prefix), "%s",
           cfg.discovery_prefix[0] != '\0' ? cfg.discovery_prefix : "homeassistant");
  _sanitise(s_node_id, sizeof(s_node_id), identity_thing_id());
  snprintf(s_dev_name, sizeof(s_dev_name), "RebelEspresso");
  snprintf(s_model, sizeof(s_model), "rebel-espresso");
}

static void _build_state(mqtt_state_t *s) {
  const process_image_t *img = process_image_get();
  measure_t boiler = {};
  measure_t brew = {};
  rtds_get(&boiler, RTD_BREW_BOILER_IDX);
  rtds_get(&brew, RTD_BREW_HEAD_IDX);

  s->boiler_temp = (float)boiler.value;
  s->boiler_setpoint = (float)boiler_temp_get_current_setpoint();
  s->brew_temp = (float)brew.value;
  s->brew_setpoint = (float)brew_temp_get_setpoint();
  s->boiler_duty = boiler_temp_get_duty();
  s->water_level_mv = (int)img->water_level_mv;
  s->probe_mv = img->water_level_median_mv;
  s->corrosion_status = img->corrosion_status;
  s->power = power_is_active();
  s->brewing = img->brew_active;
  s->steam = img->steam_on;
  s->descale = img->descale_mode;

  RefillState_t rs = boiler_refill_state();
  s->refill_active = (rs == REFILL_STATE_ACTIVE);
  s->refill_error = (rs == REFILL_STATE_ERROR);
}

static void _publish_state() {
  if (!s_connected) {
    return;
  }
  mqtt_state_t st = {};
  _build_state(&st);
  char *json = mqtt_build_state_json(&st);
  if (json) {
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/state", s_base);
    esp_mqtt_client_publish(s_client, topic, json, 0, 0, 0);
    free(json);
  }
  s_last_state_us = esp_timer_get_time();
}

static void _publish_discovery() {
  const esp_app_desc_t *app = esp_app_get_description();
  size_t n = 0;
  const mqtt_entity_t *ents = mqtt_entities(&n);
  for (size_t i = 0; i < n; i++) {
    char *cfg =
        mqtt_build_discovery_json(&ents[i], s_base, s_avail_topic, s_node_id, s_dev_name, s_model, app->version);
    if (!cfg) {
      continue;
    }
    char topic[192];
    snprintf(topic, sizeof(topic), "%s/%s/%s/%s/config", s_disc_prefix, ents[i].component, s_node_id,
             ents[i].object_id);
    esp_mqtt_client_publish(s_client, topic, cfg, 0, 1, 1); // retained
    free(cfg);
  }
  ESP_LOGI(TAG, "Published discovery for %u entities", (unsigned)n);
}

static void _event_handler([[maybe_unused]] void *args, [[maybe_unused]] esp_event_base_t base, int32_t id,
                           [[maybe_unused]] void *data) {
  switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(TAG, "Connected — publishing availability + discovery");
      s_connected = true;
      esp_mqtt_client_publish(s_client, s_avail_topic, "online", 0, 1, 1);
      _publish_discovery();
      _publish_state();
      break;
    case MQTT_EVENT_DISCONNECTED:
      ESP_LOGW(TAG, "Disconnected");
      s_connected = false;
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
    // Already running (esp-mqtt owns reconnect). Publish state at the configured
    // cadence while connected.
    if (s_started && s_connected) {
      uint32_t interval_us = (cfg.publish_interval_sec ? cfg.publish_interval_sec : 5) * 1000000u;
      if (esp_timer_get_time() - s_last_state_us >= interval_us) {
        _publish_state();
      }
    }
    return;
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
  s_connected = false;
}
