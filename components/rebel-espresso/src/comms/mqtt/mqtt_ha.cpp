#include "mqtt_ha.h"
#include "mqtt_config.h"
#include "mqtt_discovery.h"

#include <mqtt_client.h> // esp-mqtt (ESP-IDF)
#include <esp_log.h>
#include <esp_app_desc.h>
#include <esp_timer.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include "common/identity.h"
#include "process_image.h"
#include "boiler_temp.h"
#include "brew_temp.h"
#include "boiler_refill.h"
#include "boiler_refill_states.h"
#include "rtds.h"
#include "power.h"
#include "wifi/wifi_manager.h"
#include <hw_config.h>

#define TAG "mqtt_ha"

static esp_mqtt_client_handle_t s_client = nullptr;
static bool s_started = false;
static bool s_connected = false;
static char s_base[80];
static char s_avail_topic[96];
static char s_disc_prefix[32];
static char s_node_id[80];
static char s_client_id[48];
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
  // HA best practice: a base topic unique per device, derived from the device
  // identity (thing type + id). The discovery prefix is the fixed HA namespace,
  // and the client id is derived from the thing id.
  char ttype[32];
  char tid[40];
  _sanitise(ttype, sizeof(ttype), identity_get()->thing_type);
  _sanitise(tid, sizeof(tid), identity_thing_id());

  snprintf(s_base, sizeof(s_base), "%s/%s", ttype, tid);
  snprintf(s_avail_topic, sizeof(s_avail_topic), "%s/availability", s_base);
  snprintf(s_disc_prefix, sizeof(s_disc_prefix), "homeassistant");
  snprintf(s_node_id, sizeof(s_node_id), "%s", tid);
  snprintf(s_client_id, sizeof(s_client_id), "%s", tid);
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

  // Advertise the on-device web UI so HA shows a "Visit device" link (device
  // info page). Recomputed here so it always reflects the current STA IP.
  char config_url[40] = "";
  wifi_metrics_t wm = wifi_manager_get_metrics();
  if (wm.ip_addr[0] != '\0') {
    snprintf(config_url, sizeof(config_url), "http://%s:8080", wm.ip_addr);
  }

  size_t n = 0;
  const mqtt_entity_t *ents = mqtt_entities(&n);
  for (size_t i = 0; i < n; i++) {
    char *cfg = mqtt_build_discovery_json(&ents[i], s_base, s_avail_topic, s_node_id, s_dev_name, s_model, app->version,
                                          config_url);
    if (!cfg) {
      continue;
    }
    char topic[192];
    snprintf(topic, sizeof(topic), "%s/%s/%s/%s/config", s_disc_prefix, ents[i].component, s_node_id,
             ents[i].object_id);
    esp_mqtt_client_publish(s_client, topic, cfg, 0, 1, 1); // retained
    free(cfg);
  }

  // Control entities (write): brew climate (carries power via off/heat mode) +
  // calibrate button.
  char ctopic[192];
  char *j;
  j = mqtt_build_brew_climate_json(s_base, s_avail_topic, s_node_id, s_dev_name, s_model, app->version, config_url);
  snprintf(ctopic, sizeof(ctopic), "%s/climate/%s/brew/config", s_disc_prefix, s_node_id);
  esp_mqtt_client_publish(s_client, ctopic, j, 0, 1, 1);
  free(j);
  j = mqtt_build_calibrate_button_json(s_base, s_avail_topic, s_node_id, s_dev_name, s_model, app->version, config_url);
  snprintf(ctopic, sizeof(ctopic), "%s/button/%s/calibrate/config", s_disc_prefix, s_node_id);
  esp_mqtt_client_publish(s_client, ctopic, j, 0, 1, 1);
  free(j);

  // Remove the deprecated standalone power switch (folded into the climate mode):
  // an empty retained payload deletes a previously-discovered entity.
  snprintf(ctopic, sizeof(ctopic), "%s/switch/%s/power/config", s_disc_prefix, s_node_id);
  esp_mqtt_client_publish(s_client, ctopic, "", 0, 1, 1);

  ESP_LOGI(TAG, "Published discovery for %u entities", (unsigned)n);
}

// True when `topic` ends with `suffix` (exact command match, not a substring).
static bool _topic_ends(const char *topic, const char *suffix) {
  size_t tl = strlen(topic), sl = strlen(suffix);
  return tl >= sl && strcmp(topic + tl - sl, suffix) == 0;
}

// Map an inbound command topic + payload onto the existing remote APIs.
static void _handle_command(const char *topic, const char *payload) {
  if (_topic_ends(topic, "/cmd/mode")) {
    // Climate mode carries power: heat = active, off = standby.
    if (strcmp(payload, "heat") == 0) {
      power_active();
    } else if (strcmp(payload, "off") == 0) {
      power_standby();
    }
  } else if (_topic_ends(topic, "/cmd/brew_setpoint")) {
    double v = atof(payload);
    if (v >= 80.0 && v <= 105.0) {
      brew_temp_set_setpoint(v);
    }
  } else if (_topic_ends(topic, "/cmd/calibrate")) {
    // HA button sends "PRESS"; only calibrate against a trusted submerged read.
    if (strcmp(payload, "PRESS") != 0 || !process_image_get()->water_level_ok) {
      return;
    }
    boiler_refill_calibrate_probe(process_image_get()->water_level_median_mv);
  } else {
    return;
  }
  _publish_state(); // reflect the accepted change immediately
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
      {
        char sub[128];
        snprintf(sub, sizeof(sub), "%s/cmd/#", s_base);
        esp_mqtt_client_subscribe(s_client, sub, 1);
      }
      break;
    case MQTT_EVENT_DATA: {
      esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;
      char topic[128];
      char payload[64];
      int tl = ev->topic_len < (int)sizeof(topic) - 1 ? ev->topic_len : (int)sizeof(topic) - 1;
      int pl = ev->data_len < (int)sizeof(payload) - 1 ? ev->data_len : (int)sizeof(payload) - 1;
      memcpy(topic, ev->topic, tl);
      topic[tl] = '\0';
      memcpy(payload, ev->data, pl);
      payload[pl] = '\0';
      _handle_command(topic, payload);
      break;
    }
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
  mcfg.credentials.client_id = s_client_id; // derived from thing id
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
