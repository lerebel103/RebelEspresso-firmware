#pragma once

#include <cJSON.h>
#include <cstdint>

/**
 * MQTT connection configuration block. Follows the same from_json/to_json +
 * NVS pattern as the other config sections, but (like `schedules`) persists as
 * a single JSON blob so it can carry strings. The password is write-only over
 * the API: it is redacted in to_json() and only overwritten by from_json() when
 * a non-empty value is supplied.
 *
 * Topics, client id and discovery prefix are NOT configured here: they are
 * derived by mqtt_ha from the device identity (thing type + id) and the fixed
 * Home Assistant discovery prefix, per HA best practice.
 */

#define NVS_MQTT_CFG_STORE "cfg.mqtt"

struct mqtt_cfg_t {
  bool enabled = false;
  char broker_uri[128] = {0}; ///< e.g. mqtt://host:1883 (plain TCP)
  char username[64] = {0};
  char password[96] = {0}; ///< redacted in to_json()
  uint16_t publish_interval_sec = 5;

  void from_json(const cJSON *config);
  /// Emit config; password is redacted (empty) with a `has_password` flag.
  void to_json(cJSON *config, const char *base_key) const;
};

/// Load config from NVS (defaults if absent). Call once at init.
void mqtt_config_load();

const mqtt_cfg_t& mqtt_config_get();

/// Merge a partial JSON config and persist to NVS.
void mqtt_config_update(const cJSON *json);

/// Erase and reload defaults.
void mqtt_config_reset();
