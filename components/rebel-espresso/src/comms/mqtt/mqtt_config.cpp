#include "mqtt_config.h"

#include <cstring>
#include <nvs.h>
#include <esp_log.h>

#define TAG "mqtt_cfg"
#define NVS_KEY_MQTT "mqtt"

static mqtt_cfg_t s_cfg;

static void _copy_str(char *dst, size_t size, const char *src) {
  if (src == nullptr) {
    return;
  }
  snprintf(dst, size, "%s", src);
}

static const char *_get_str(const cJSON *item) {
  const char *s = cJSON_GetStringValue(item);
  return s ? s : "";
}

void mqtt_cfg_t::from_json(const cJSON *config) {
  cJSON *item = config->child;
  while (item) {
    if (strcmp(item->string, "enabled") == 0) {
      enabled = cJSON_IsTrue(item);
    } else if (strcmp(item->string, "broker_uri") == 0) {
      _copy_str(broker_uri, sizeof(broker_uri), _get_str(item));
    } else if (strcmp(item->string, "username") == 0) {
      _copy_str(username, sizeof(username), _get_str(item));
    } else if (strcmp(item->string, "password") == 0) {
      // Write-only: only overwrite when a non-empty password is supplied.
      const char *pw = _get_str(item);
      if (pw[0] != '\0') {
        _copy_str(password, sizeof(password), pw);
      }
    } else if (strcmp(item->string, "client_id") == 0) {
      _copy_str(client_id, sizeof(client_id), _get_str(item));
    } else if (strcmp(item->string, "base_topic") == 0) {
      _copy_str(base_topic, sizeof(base_topic), _get_str(item));
    } else if (strcmp(item->string, "discovery_prefix") == 0) {
      _copy_str(discovery_prefix, sizeof(discovery_prefix), _get_str(item));
    } else if (strcmp(item->string, "publish_interval_sec") == 0) {
      publish_interval_sec = (uint16_t)item->valueint;
    }
    item = item->next;
  }
}

void mqtt_cfg_t::to_json(cJSON *config, const char *base_key) const {
  (void)base_key;
  cJSON_AddBoolToObject(config, "enabled", enabled);
  cJSON_AddStringToObject(config, "broker_uri", broker_uri);
  cJSON_AddStringToObject(config, "username", username);
  cJSON_AddStringToObject(config, "password", ""); // redacted
  cJSON_AddBoolToObject(config, "has_password", password[0] != '\0');
  cJSON_AddStringToObject(config, "client_id", client_id);
  cJSON_AddStringToObject(config, "base_topic", base_topic);
  cJSON_AddStringToObject(config, "discovery_prefix", discovery_prefix);
  cJSON_AddNumberToObject(config, "publish_interval_sec", publish_interval_sec);
}

static void _save() {
  nvs_handle_t h;
  ESP_ERROR_CHECK(nvs_open(NVS_MQTT_CFG_STORE, NVS_READWRITE, &h));

  cJSON *root = cJSON_CreateObject();
  s_cfg.to_json(root, "");
  // Persist the real password (to_json redacts it), so replace the field.
  cJSON_DeleteItemFromObject(root, "password");
  cJSON_AddStringToObject(root, "password", s_cfg.password);
  cJSON_DeleteItemFromObject(root, "has_password");

  char *content = cJSON_PrintUnformatted(root);
  esp_err_t err = nvs_set_str(h, NVS_KEY_MQTT, content);
  if (err == ESP_OK) {
    nvs_commit(h);
  } else {
    ESP_LOGE(TAG, "Failed to save mqtt config: %s", esp_err_to_name(err));
  }

  cJSON_free(content);
  cJSON_Delete(root);
  nvs_close(h);
}

void mqtt_config_load() {
  s_cfg = mqtt_cfg_t{};

  nvs_handle_t h;
  ESP_ERROR_CHECK(nvs_open(NVS_MQTT_CFG_STORE, NVS_READWRITE, &h));

  size_t len = 0;
  esp_err_t err = nvs_get_str(h, NVS_KEY_MQTT, nullptr, &len);
  if (err == ESP_OK && len > 0) {
    char *buffer = (char *)calloc(1, len);
    if (buffer && nvs_get_str(h, NVS_KEY_MQTT, buffer, &len) == ESP_OK) {
      cJSON *root = cJSON_Parse(buffer);
      if (root) {
        // Load the stored password directly (from_json treats empty as "keep").
        cJSON *pw = cJSON_GetObjectItem(root, "password");
        if (pw && cJSON_IsString(pw)) {
          _copy_str(s_cfg.password, sizeof(s_cfg.password), cJSON_GetStringValue(pw));
        }
        s_cfg.from_json(root);
        cJSON_Delete(root);
      }
    }
    free(buffer);
  }

  nvs_close(h);
}

const mqtt_cfg_t& mqtt_config_get() {
  return s_cfg;
}

void mqtt_config_update(const cJSON *json) {
  s_cfg.from_json(json);
  _save();
}

void mqtt_config_reset() {
  nvs_handle_t h;
  ESP_ERROR_CHECK(nvs_open(NVS_MQTT_CFG_STORE, NVS_READWRITE, &h));
  nvs_erase_all(h);
  nvs_commit(h);
  nvs_close(h);

  s_cfg = mqtt_cfg_t{};
}
