/**
 * MQTT Home Assistant discovery + state serialisation tests (pure).
 */
#include <unity.h>
#include <cJSON.h>
#include <cstdlib>
#include <cstring>

#include "mqtt/mqtt_discovery.h"

TEST_CASE("MQTT state: JSON document carries all fields", "[mqtt]") {
  mqtt_state_t s = {};
  s.boiler_temp = 92.5f;
  s.boiler_setpoint = 105.0f;
  s.boiler_duty = 40;
  s.power = true;
  s.refill_error = false;

  char *json = mqtt_build_state_json(&s);
  TEST_ASSERT_NOT_NULL(json);

  cJSON *root = cJSON_Parse(json);
  TEST_ASSERT_NOT_NULL(root);
  TEST_ASSERT_EQUAL_FLOAT(92.5f, (float)cJSON_GetObjectItem(root, "boiler_temp")->valuedouble);
  TEST_ASSERT_EQUAL_INT(40, cJSON_GetObjectItem(root, "boiler_duty")->valueint);
  TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "power")));
  TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(root, "refill_error")));

  cJSON_Delete(root);
  free(json);
}

TEST_CASE("MQTT discovery: sensor config has topics, template, uid and device", "[mqtt]") {
  size_t n = 0;
  const mqtt_entity_t *ents = mqtt_entities(&n);
  TEST_ASSERT_TRUE(n > 0);

  // Find the boiler_temp sensor.
  const mqtt_entity_t *boiler = nullptr;
  for (size_t i = 0; i < n; i++) {
    if (strcmp(ents[i].object_id, "boiler_temp") == 0) {
      boiler = &ents[i];
    }
  }
  TEST_ASSERT_NOT_NULL(boiler);

  char *json =
      mqtt_build_discovery_json(boiler, "rebel/abc", "rebel/abc/availability", "rebel_abc", "Rebel", "r2", "1.2.3");
  cJSON *root = cJSON_Parse(json);
  TEST_ASSERT_NOT_NULL(root);

  TEST_ASSERT_EQUAL_STRING("rebel_abc_boiler_temp", cJSON_GetObjectItem(root, "unique_id")->valuestring);
  TEST_ASSERT_EQUAL_STRING("rebel/abc/state", cJSON_GetObjectItem(root, "state_topic")->valuestring);
  TEST_ASSERT_EQUAL_STRING("rebel/abc/availability", cJSON_GetObjectItem(root, "availability_topic")->valuestring);
  TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(root, "value_template"));

  cJSON *device = cJSON_GetObjectItem(root, "device");
  TEST_ASSERT_NOT_NULL(device);
  TEST_ASSERT_EQUAL_STRING("1.2.3", cJSON_GetObjectItem(device, "sw_version")->valuestring);
  cJSON *ids = cJSON_GetObjectItem(device, "identifiers");
  TEST_ASSERT_EQUAL_STRING("rebel_abc", cJSON_GetArrayItem(ids, 0)->valuestring);

  cJSON_Delete(root);
  free(json);
}

TEST_CASE("MQTT discovery: device advertises configuration_url when provided", "[mqtt]") {
  size_t n = 0;
  const mqtt_entity_t *ents = mqtt_entities(&n);
  const mqtt_entity_t *e = &ents[0];

  // With a URL: the device block carries configuration_url so HA links to the UI.
  char *json = mqtt_build_discovery_json(e, "rebel/abc", "rebel/abc/availability", "rebel_abc", "Rebel", "r2", "1.0",
                                         "http://192.168.1.5:8080");
  cJSON *root = cJSON_Parse(json);
  cJSON *device = cJSON_GetObjectItem(root, "device");
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.5:8080", cJSON_GetObjectItem(device, "configuration_url")->valuestring);
  cJSON_Delete(root);
  free(json);

  // Without a URL (nullptr default): the key is omitted.
  json = mqtt_build_discovery_json(e, "rebel/abc", "rebel/abc/availability", "rebel_abc", "Rebel", "r2", "1.0");
  root = cJSON_Parse(json);
  device = cJSON_GetObjectItem(root, "device");
  TEST_ASSERT_NULL(cJSON_GetObjectItem(device, "configuration_url"));
  cJSON_Delete(root);
  free(json);
}

TEST_CASE("MQTT discovery: binary_sensor renders ON/OFF template", "[mqtt]") {
  size_t n = 0;
  const mqtt_entity_t *ents = mqtt_entities(&n);
  const mqtt_entity_t *power = nullptr;
  for (size_t i = 0; i < n; i++) {
    if (strcmp(ents[i].object_id, "power") == 0) {
      power = &ents[i];
    }
  }
  TEST_ASSERT_NOT_NULL(power);
  TEST_ASSERT_EQUAL_STRING("binary_sensor", power->component);
  TEST_ASSERT_NOT_NULL(strstr(power->value_template, "'ON'"));
  TEST_ASSERT_NOT_NULL(strstr(power->value_template, "'OFF'"));
}

TEST_CASE("MQTT discovery: probe voltage is a diagnostic entity", "[mqtt]") {
  size_t n = 0;
  const mqtt_entity_t *ents = mqtt_entities(&n);
  const mqtt_entity_t *probe = nullptr;
  for (size_t i = 0; i < n; i++) {
    if (strcmp(ents[i].object_id, "probe_voltage") == 0) {
      probe = &ents[i];
    }
  }
  TEST_ASSERT_NOT_NULL(probe);
  TEST_ASSERT_EQUAL_STRING("diagnostic", probe->entity_category);
  TEST_ASSERT_EQUAL_STRING("mV", probe->unit);
}

TEST_CASE("MQTT controls: brew climate carries power via off/heat mode", "[mqtt]") {
  char *json = mqtt_build_brew_climate_json("rebel/abc", "rebel/abc/availability", "rebel_abc", "Rebel", "r2", "1.0");
  cJSON *root = cJSON_Parse(json);
  TEST_ASSERT_NOT_NULL(root);
  TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(root, "current_temperature_template"));
  TEST_ASSERT_EQUAL_STRING("rebel/abc/cmd/brew_setpoint",
                           cJSON_GetObjectItem(root, "temperature_command_topic")->valuestring);
  TEST_ASSERT_EQUAL_STRING("rebel/abc/cmd/mode", cJSON_GetObjectItem(root, "mode_command_topic")->valuestring);

  cJSON *modes = cJSON_GetObjectItem(root, "modes");
  TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(modes));
  TEST_ASSERT_EQUAL_STRING("off", cJSON_GetArrayItem(modes, 0)->valuestring);
  TEST_ASSERT_EQUAL_STRING("heat", cJSON_GetArrayItem(modes, 1)->valuestring);
  cJSON_Delete(root);
  free(json);
}

TEST_CASE("MQTT controls: calibrate button issues a press command", "[mqtt]") {
  char *json =
      mqtt_build_calibrate_button_json("rebel/abc", "rebel/abc/availability", "rebel_abc", "Rebel", "r2", "1.0");
  cJSON *root = cJSON_Parse(json);
  TEST_ASSERT_NOT_NULL(root);
  TEST_ASSERT_EQUAL_STRING("rebel/abc/cmd/calibrate", cJSON_GetObjectItem(root, "command_topic")->valuestring);
  TEST_ASSERT_EQUAL_STRING("PRESS", cJSON_GetObjectItem(root, "payload_press")->valuestring);
  cJSON_Delete(root);
  free(json);
}
