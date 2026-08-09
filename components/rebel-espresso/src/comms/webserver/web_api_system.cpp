#include "web_api_system.h"

#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_image_format.h>
#include <cJSON.h>
#include <nvs_flash.h>
#include <cstring>

#include "web_auth.h"
#include "common/identity.h"
#include "app_metrics.h"
#include "brew.h"
#include "boiler_temp.h"
#include "brew_temp.h"
#include "boiler_refill.h"
#include "schedules.h"
#include "process_image.h"
#include "mqtt/mqtt_ha.h"
#include "mqtt/mqtt_config.h"
#include "homekit/homekit.h"
#include "homekit/homekit_config.h"
#include <src/device/thing_info.h>

#define TAG "api_system"
#define OTA_BUF_SIZE 4096

// --- GET /api/system/info ---

static esp_err_t _info_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;
  const esp_app_desc_t *app = esp_app_get_description();
  auto identity = identity_get();
  auto metrics = app_metrics_get();

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "firmware_version", app->version);
  cJSON_AddStringToObject(root, "project_name", app->project_name);
  cJSON_AddStringToObject(root, "idf_version", app->idf_ver);
  cJSON_AddStringToObject(root, "build_date", app->date);
  cJSON_AddStringToObject(root, "build_time", app->time);
  cJSON_AddStringToObject(root, "thing_type", identity->thing_type);
  cJSON_AddStringToObject(root, "thing_id", identity_thing_id());
  // Report the authoritative EEPROM-backed device identity (same source as the
  // console "Hardware Info"), not the provisioned NVS identity value which may be stale.
  const thing_info_ext_t *ti = thing_info_ext();
  cJSON_AddNumberToObject(root, "hardware_rev", ti->hardware_version_major);
  cJSON_AddNumberToObject(root, "hardware_rev_minor", ti->hardware_version_minor);
  cJSON_AddNumberToObject(root, "serial", (double)ti->serial);
  cJSON_AddNumberToObject(root, "manufacturer_id", ti->manufacturer_id);
  cJSON_AddNumberToObject(root, "build_epoch", (double)ti->build_epoch_s);
  cJSON_AddNumberToObject(root, "boot_count", metrics.boot_count);
  cJSON_AddNumberToObject(root, "crash_count", metrics.crash_count);
  cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());
  cJSON_AddNumberToObject(root, "min_free_heap", (double)esp_get_minimum_free_heap_size());
  cJSON_AddNumberToObject(root, "uptime_sec", (double)(esp_timer_get_time() / 1000000));

  // Maintenance stats
  auto brew_status = brew_get_status();
  cJSON_AddNumberToObject(root, "descale_count", brew_status.descale_count);
  cJSON_AddNumberToObject(root, "last_descale_time", (double)brew_status.last_descale_time);

  // Water-probe diagnostic (median voltage) + corrosion monitoring
  const process_image_t *pi = process_image_get();
  auto& refill_cfg = boiler_refill_get_cfg();
  cJSON_AddNumberToObject(root, "water_probe_mv", pi->water_level_median_mv);
  cJSON_AddNumberToObject(root, "corrosion_status", pi->corrosion_status);
  cJSON_AddBoolToObject(root, "corrosion_enabled", refill_cfg.corrosion_enabled != 0);
  cJSON_AddBoolToObject(root, "corrosion_guard_enabled", refill_cfg.corrosion_guard_enabled != 0);
  cJSON_AddNumberToObject(root, "corrosion_baseline_mv", refill_cfg.corrosion_baseline_mv);
  cJSON_AddNumberToObject(root, "corrosion_warn_mv", refill_cfg.corrosion_warn_threshold_mv);
  cJSON_AddNumberToObject(root, "corrosion_fault_mv", refill_cfg.corrosion_fault_threshold_mv);

  const char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, json);

  cJSON_free((void *)json);
  cJSON_Delete(root);
  return ESP_OK;
}

// --- POST /api/system/ota ---

static esp_err_t _ota_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  ESP_LOGI(TAG, "OTA update started, content length: %d", req->content_len);

  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition available");
    return ESP_FAIL;
  }

  // Validate size against partition
  if (req->content_len > (int)update_partition->size) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware too large for OTA partition");
    return ESP_FAIL;
  }

  esp_ota_handle_t ota_handle;
  esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
    return ESP_FAIL;
  }

  char *buf = (char *)malloc(OTA_BUF_SIZE);
  if (!buf) {
    esp_ota_abort(ota_handle);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  int remaining = req->content_len;
  bool header_checked = false;

  while (remaining > 0) {
    int recv_len = httpd_req_recv(req, buf, (remaining < OTA_BUF_SIZE) ? remaining : OTA_BUF_SIZE);
    if (recv_len <= 0) {
      if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      ESP_LOGE(TAG, "OTA receive error");
      free(buf);
      esp_ota_abort(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
      return ESP_FAIL;
    }

    // Validate first chunk (header)
    if (!header_checked && recv_len >= 48) {
      // Check ESP32 image magic byte
      if ((uint8_t)buf[0] != 0xE9) {
        ESP_LOGE(TAG, "Invalid firmware: bad magic byte 0x%02X", (uint8_t)buf[0]);
        free(buf);
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware image (bad magic)");
        return ESP_FAIL;
      }

      // Check project name at offset 0x30 (inside esp_app_desc_t)
      // esp_app_desc_t starts at offset 0x20 in the image, project_name is at +0x30 from there
      if (recv_len >= (int)(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))) {
        const esp_app_desc_t *incoming_desc =
            (const esp_app_desc_t *)(buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
        const esp_app_desc_t *running_desc = esp_app_get_description();

        if (strcmp(incoming_desc->project_name, running_desc->project_name) != 0) {
          ESP_LOGE(TAG, "Project name mismatch: '%s' vs '%s'", incoming_desc->project_name, running_desc->project_name);
          char err_msg[160];
          snprintf(err_msg, sizeof(err_msg), "Wrong firmware: expected '%s', got '%s'", running_desc->project_name,
                   incoming_desc->project_name);
          free(buf);
          esp_ota_abort(ota_handle);
          httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err_msg);
          return ESP_FAIL;
        }

        ESP_LOGI(TAG, "OTA: %s -> %s", running_desc->version, incoming_desc->version);
      }
      header_checked = true;
    }

    err = esp_ota_write(ota_handle, buf, recv_len);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
      free(buf);
      esp_ota_abort(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
      return ESP_FAIL;
    }

    remaining -= recv_len;
  }

  free(buf);

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed (image invalid)");
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"OTA complete, rebooting...\"}");

  // Delay briefly to allow response to be sent, then reboot
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();

  return ESP_OK; // unreachable
}

// --- GET /api/system/config-export ---
// Streams config one section at a time to minimise heap usage.
// Only one section's JSON is in memory at any moment.

#define CONFIG_MAX_IMPORT_SIZE 4096

static esp_err_t _config_export_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"config.json\"");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // Stream JSON object section by section via chunked transfer.
  // Only one section's cJSON object is in memory at any moment.
  httpd_resp_send_chunk(req, "{", 1);

  // boiler_temp
  {
    const char *key = "\"boiler_temp\":";
    httpd_resp_send_chunk(req, key, strlen(key));
    cJSON *obj = cJSON_CreateObject();
    auto cfg = boiler_temp_get_cfg();
    cfg.to_json(obj, "");
    char *s = cJSON_PrintUnformatted(obj);
    httpd_resp_send_chunk(req, s, strlen(s));
    cJSON_free(s);
    cJSON_Delete(obj);
  }
  // brew_temp
  {
    const char *key = ",\"brew_temp\":";
    httpd_resp_send_chunk(req, key, strlen(key));
    cJSON *obj = cJSON_CreateObject();
    auto cfg = brew_temp_get_cfg();
    cfg.to_json(obj, "");
    char *s = cJSON_PrintUnformatted(obj);
    httpd_resp_send_chunk(req, s, strlen(s));
    cJSON_free(s);
    cJSON_Delete(obj);
  }
  // boiler_refill
  {
    const char *key = ",\"boiler_refill\":";
    httpd_resp_send_chunk(req, key, strlen(key));
    cJSON *obj = cJSON_CreateObject();
    auto cfg = boiler_refill_get_cfg();
    cfg.to_json(obj, "");
    char *s = cJSON_PrintUnformatted(obj);
    httpd_resp_send_chunk(req, s, strlen(s));
    cJSON_free(s);
    cJSON_Delete(obj);
  }
  // schedules
  {
    const char *key = ",\"schedules\":";
    httpd_resp_send_chunk(req, key, strlen(key));
    cJSON *obj = cJSON_CreateObject();
    auto cfg = schedules_get_cfg();
    cfg.to_json(obj, "");
    char *s = cJSON_PrintUnformatted(obj);
    httpd_resp_send_chunk(req, s, strlen(s));
    cJSON_free(s);
    cJSON_Delete(obj);
  }
  // mqtt (password is redacted by to_json — secrets are never exported)
  {
    const char *key = ",\"mqtt\":";
    httpd_resp_send_chunk(req, key, strlen(key));
    cJSON *obj = cJSON_CreateObject();
    mqtt_config_get().to_json(obj, "");
    char *s = cJSON_PrintUnformatted(obj);
    httpd_resp_send_chunk(req, s, strlen(s));
    cJSON_free(s);
    cJSON_Delete(obj);
  }
  // homekit
  {
    const char *key = ",\"homekit\":";
    httpd_resp_send_chunk(req, key, strlen(key));
    cJSON *obj = cJSON_CreateObject();
    homekit_config_get().to_json(obj, "");
    char *s = cJSON_PrintUnformatted(obj);
    httpd_resp_send_chunk(req, s, strlen(s));
    cJSON_free(s);
    cJSON_Delete(obj);
  }

  httpd_resp_send_chunk(req, "}", 1);
  httpd_resp_send_chunk(req, NULL, 0); // end chunked transfer
  return ESP_OK;
}

// --- POST /api/system/config-import ---
// Accepts a JSON body (max 4KB) and applies each section found.

static esp_err_t _config_import_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  if (req->content_len <= 0 || req->content_len >= CONFIG_MAX_IMPORT_SIZE) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body empty or too large (max 4KB)");
    return ESP_FAIL;
  }

  char *buf = (char *)malloc(req->content_len + 1);
  if (!buf) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  int total = req->content_len;
  int received = 0;
  while (received < total) {
    int ret = httpd_req_recv(req, buf + received, total - received);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        continue;
      free(buf);
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Receive error");
      return ESP_FAIL;
    }
    received += ret;
  }
  buf[received] = '\0';

  cJSON *root = cJSON_Parse(buf);
  free(buf); // free the raw buffer immediately

  if (!root) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  int applied = 0;

  // Apply each section if present
  cJSON *section = cJSON_GetObjectItem(root, "boiler_temp");
  if (section) {
    boiler_temp_update_cfg(section);
    applied++;
  }

  section = cJSON_GetObjectItem(root, "brew_temp");
  if (section) {
    brew_temp_update_cfg(section);
    applied++;
  }

  section = cJSON_GetObjectItem(root, "boiler_refill");
  if (section) {
    boiler_refill_update_cfg(section);
    applied++;
  }

  section = cJSON_GetObjectItem(root, "schedules");
  if (section) {
    schedules_update_cfg(section);
    applied++;
  }

  section = cJSON_GetObjectItem(root, "mqtt");
  if (section) {
    mqtt_config_update(section);
    applied++;
  }

  section = cJSON_GetObjectItem(root, "homekit");
  if (section) {
    homekit_config_update(section);
    applied++;
  }

  cJSON_Delete(root);

  char resp[64];
  snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"sections_applied\":%d}", applied);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, resp);

  ESP_LOGI(TAG, "Config import: %d sections applied", applied);
  return ESP_OK;
}

// --- POST /api/system/reboot ---

static esp_err_t _reboot_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Rebooting...\"}");

  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
  return ESP_OK;
}

// --- POST /api/system/factory-reset ---

static esp_err_t _factory_reset_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  ESP_LOGW(TAG, "Factory reset requested!");

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Factory reset, rebooting...\"}");

  vTaskDelay(pdMS_TO_TICKS(500));
  nvs_flash_erase();
  esp_restart();
  return ESP_OK;
}

// --- POST /api/system/probe-calibrate ---

static esp_err_t _probe_calibrate_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  // Only calibrate against a valid, submerged reading (ADC OK and below the
  // refill threshold) — otherwise (empty boiler, ADC fault or startup) we would
  // store a bogus baseline and silently disable corrosion protection until
  // re-calibrated.
  const process_image_t *pi = process_image_get();
  if (!pi->water_level_ok) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "Probe calibration requires a valid, submerged probe reading (boiler full, ADC OK)");
    return ESP_FAIL;
  }
  uint16_t median = pi->water_level_median_mv;
  boiler_refill_calibrate_probe(median);
  auto& cfg = boiler_refill_get_cfg();

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "status", "ok");
  cJSON_AddNumberToObject(root, "baseline_mv", cfg.corrosion_baseline_mv);
  cJSON_AddNumberToObject(root, "warn_mv", cfg.corrosion_warn_threshold_mv);
  cJSON_AddNumberToObject(root, "fault_mv", cfg.corrosion_fault_threshold_mv);

  const char *resp = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, resp);

  cJSON_free((void *)resp);
  cJSON_Delete(root);
  return ESP_OK;
}

// --- GET /api/comms/status (live MQTT + HomeKit connection state) ---

static esp_err_t _comms_status_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  cJSON *root = cJSON_CreateObject();

  cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
  cJSON_AddBoolToObject(mqtt, "enabled", mqtt_ha_is_enabled());
  cJSON_AddBoolToObject(mqtt, "connected", mqtt_ha_is_connected());

  cJSON *hk = cJSON_AddObjectToObject(root, "homekit");
  const homekit_cfg_t& hkcfg = homekit_config_get();
  cJSON_AddBoolToObject(hk, "enabled", hkcfg.enabled);
  cJSON_AddBoolToObject(hk, "running", homekit_is_running());
  cJSON_AddNumberToObject(hk, "paired", homekit_paired_count());

  const char *resp = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, resp);
  cJSON_free((void *)resp);
  cJSON_Delete(root);
  return ESP_OK;
}

// --- POST /api/homekit/reset-pairings ---

static esp_err_t _homekit_reset_pairings_handler(httpd_req_t *req) {
  if (!web_auth_check(req))
    return ESP_FAIL;

  ESP_LOGW(TAG, "HomeKit reset-pairings requested!");

  if (!homekit_reset_pairings()) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "HomeKit is not running");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"HomeKit pairings erased, accessory rebooting...\"}");
  return ESP_OK;
}

void web_api_system_register(httpd_handle_t server) {
  const httpd_uri_t info_uri = {
      .uri = "/api/system/info",
      .method = HTTP_GET,
      .handler = _info_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &info_uri);

  const httpd_uri_t ota_uri = {
      .uri = "/api/system/ota",
      .method = HTTP_POST,
      .handler = _ota_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &ota_uri);

  const httpd_uri_t reboot_uri = {
      .uri = "/api/system/reboot",
      .method = HTTP_POST,
      .handler = _reboot_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &reboot_uri);

  const httpd_uri_t config_export_uri = {
      .uri = "/api/system/config-export",
      .method = HTTP_GET,
      .handler = _config_export_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &config_export_uri);

  const httpd_uri_t config_import_uri = {
      .uri = "/api/system/config-import",
      .method = HTTP_POST,
      .handler = _config_import_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &config_import_uri);

  const httpd_uri_t factory_reset_uri = {
      .uri = "/api/system/factory-reset",
      .method = HTTP_POST,
      .handler = _factory_reset_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &factory_reset_uri);

  const httpd_uri_t probe_cal_uri = {
      .uri = "/api/system/probe-calibrate",
      .method = HTTP_POST,
      .handler = _probe_calibrate_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &probe_cal_uri);

  const httpd_uri_t comms_status_uri = {
      .uri = "/api/comms/status",
      .method = HTTP_GET,
      .handler = _comms_status_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &comms_status_uri);

  const httpd_uri_t hk_reset_uri = {
      .uri = "/api/homekit/reset-pairings",
      .method = HTTP_POST,
      .handler = _homekit_reset_pairings_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &hk_reset_uri);

  ESP_LOGI(TAG, "System API registered");
}
