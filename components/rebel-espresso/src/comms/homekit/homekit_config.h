#pragma once

#include <cJSON.h>

/**
 * HomeKit integration configuration. Persisted as a single JSON blob in NVS,
 * following the same load/get/update/reset pattern as the MQTT config.
 *
 * `setup_code` is firmware-owned so the pairing PIN is deterministic and can be
 * shown in the UI. It is applied via hap_set_setup_code() before hap_start(),
 * overriding any code derived from the factory keystore.
 */

#define NVS_HOMEKIT_CFG_STORE "cfg.homekit"

struct homekit_cfg_t {
  bool enabled = true;
  char setup_code[11] = "111-22-333"; ///< pairing PIN, format xxx-xx-xxx

  void from_json(const cJSON *config);
  void to_json(cJSON *config, const char *base_key) const;
};

/// Load config from NVS (defaults if absent). Call once at init.
void homekit_config_load();

const homekit_cfg_t& homekit_config_get();

/// Merge a partial JSON config and persist to NVS.
void homekit_config_update(const cJSON *json);

/// Erase and reload defaults.
void homekit_config_reset();

/// True when `code` matches the required "xxx-xx-xxx" digit format.
bool homekit_setup_code_valid(const char *code);
