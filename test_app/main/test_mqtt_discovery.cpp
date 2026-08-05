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
