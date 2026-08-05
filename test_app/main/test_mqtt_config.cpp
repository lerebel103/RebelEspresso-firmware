/**
 * MQTT config block tests — JSON round-trip and write-only password redaction.
 */
#include <unity.h>
#include <cJSON.h>
#include <cstring>

#include "mqtt/mqtt_config.h"

TEST_CASE("MQTT config: JSON round-trip preserves fields", "[mqtt]") {
  mqtt_cfg_t a{};
  a.enabled = true;
  strcpy(a.broker_uri, "mqtt://192.168.1.10:1883");
  strcpy(a.username, "espresso");
  strcpy(a.password, "s3cret");
  a.publish_interval_sec = 10;

  cJSON *j = cJSON_CreateObject();
  a.to_json(j, "");

  mqtt_cfg_t b{};
  b.from_json(j);

  TEST_ASSERT_TRUE(b.enabled);
  TEST_ASSERT_EQUAL_STRING("mqtt://192.168.1.10:1883", b.broker_uri);
  TEST_ASSERT_EQUAL_STRING("espresso", b.username);
  TEST_ASSERT_EQUAL_UINT16(10, b.publish_interval_sec);
  cJSON_Delete(j);
}

TEST_CASE("MQTT config: password is redacted in to_json", "[mqtt]") {
  mqtt_cfg_t a{};
  strcpy(a.password, "topsecret");

  cJSON *j = cJSON_CreateObject();
  a.to_json(j, "");
  TEST_ASSERT_EQUAL_STRING("", cJSON_GetStringValue(cJSON_GetObjectItem(j, "password")));
  TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(j, "has_password")));
  cJSON_Delete(j);
}

TEST_CASE("MQTT config: empty password in from_json keeps existing", "[mqtt]") {
  mqtt_cfg_t a{};
  strcpy(a.password, "keepme");

  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "password", ""); // redacted save
  cJSON_AddStringToObject(j, "username", "newuser");
  a.from_json(j);

  TEST_ASSERT_EQUAL_STRING("keepme", a.password); // unchanged
  TEST_ASSERT_EQUAL_STRING("newuser", a.username);
  cJSON_Delete(j);
}
