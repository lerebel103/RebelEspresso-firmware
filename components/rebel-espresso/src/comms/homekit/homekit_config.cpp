#include "homekit_config.h"

#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <nvs.h>
#include <esp_log.h>

#define TAG "hk_cfg"
#define NVS_KEY_HOMEKIT "homekit"

static homekit_cfg_t s_cfg;

static void _copy_str(char *dst, size_t size, const char *src) {
  if (src == nullptr) {
    return;
  }
  snprintf(dst, size, "%s", src);
}

bool homekit_setup_code_valid(const char *code) {
  // Required Apple format: xxx-xx-xxx (10 chars, digits with hyphens at 3 & 6).
  if (code == nullptr || strlen(code) != 10) {
    return false;
  }
  for (int i = 0; i < 10; i++) {
    if (i == 3 || i == 6) {
      if (code[i] != '-') {
        return false;
      }
    } else if (!isdigit((unsigned char)code[i])) {
      return false;
    }
  }
  return true;
}

void homekit_cfg_t::from_json(const cJSON *config) {
  cJSON *item = config->child;
  while (item) {
    if (strcmp(item->string, "enabled") == 0) {
      enabled = cJSON_IsTrue(item);
    } else if (strcmp(item->string, "setup_code") == 0) {
      const char *code = cJSON_GetStringValue(item);
      // Reject malformed codes so a bad value can't brick pairing.
      if (code && homekit_setup_code_valid(code)) {
        _copy_str(setup_code, sizeof(setup_code), code);
      }
    }
    item = item->next;
  }
}

void homekit_cfg_t::to_json(cJSON *config, const char *base_key) const {
  (void)base_key;
  cJSON_AddBoolToObject(config, "enabled", enabled);
  cJSON_AddStringToObject(config, "setup_code", setup_code);
}

static void _save() {
  nvs_handle_t h;
  ESP_ERROR_CHECK(nvs_open(NVS_HOMEKIT_CFG_STORE, NVS_READWRITE, &h));

  cJSON *root = cJSON_CreateObject();
  s_cfg.to_json(root, "");
  char *content = cJSON_PrintUnformatted(root);
  esp_err_t err = nvs_set_str(h, NVS_KEY_HOMEKIT, content);
  if (err == ESP_OK) {
    nvs_commit(h);
  } else {
    ESP_LOGE(TAG, "Failed to save homekit config: %s", esp_err_to_name(err));
  }

  cJSON_free(content);
  cJSON_Delete(root);
  nvs_close(h);
}

void homekit_config_load() {
  s_cfg = homekit_cfg_t{};

  nvs_handle_t h;
  ESP_ERROR_CHECK(nvs_open(NVS_HOMEKIT_CFG_STORE, NVS_READWRITE, &h));

  size_t len = 0;
  esp_err_t err = nvs_get_str(h, NVS_KEY_HOMEKIT, nullptr, &len);
  if (err == ESP_OK && len > 0) {
    char *buffer = (char *)calloc(1, len);
    if (buffer && nvs_get_str(h, NVS_KEY_HOMEKIT, buffer, &len) == ESP_OK) {
      cJSON *root = cJSON_Parse(buffer);
      if (root) {
        s_cfg.from_json(root);
        cJSON_Delete(root);
      }
    }
    free(buffer);
  }

  nvs_close(h);
}

const homekit_cfg_t& homekit_config_get() {
  return s_cfg;
}

void homekit_config_update(const cJSON *json) {
  s_cfg.from_json(json);
  _save();
}

void homekit_config_reset() {
  nvs_handle_t h;
  ESP_ERROR_CHECK(nvs_open(NVS_HOMEKIT_CFG_STORE, NVS_READWRITE, &h));
  nvs_erase_all(h);
  nvs_commit(h);
  nvs_close(h);
  homekit_config_load();
}
