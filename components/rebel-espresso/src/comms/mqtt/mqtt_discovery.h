#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Pure Home Assistant MQTT-discovery + state serialisation. No esp-mqtt or I/O
 * dependency, so it is fully unit-testable. mqtt_ha owns the actual publishing.
 */

/// Snapshot of machine state serialised into the single JSON state document.
typedef struct {
  float boiler_temp;
  float boiler_setpoint;
  float brew_temp;
  float brew_setpoint;
  int boiler_duty;
  int water_level_mv;
  int probe_mv;
  int corrosion_status; ///< 0 OK / 1 service / 2 fault
  bool power;
  bool brewing;
  bool steam;
  bool descale;
  bool refill_active;
  bool refill_error;
} mqtt_state_t;

/// One HA entity descriptor for discovery.
typedef struct {
  const char *component;       ///< "sensor" | "binary_sensor"
  const char *object_id;       ///< e.g. "boiler_temp"
  const char *name;            ///< friendly name
  const char *value_template;  ///< Jinja over value_json
  const char *unit;            ///< unit_of_measurement, or nullptr
  const char *device_class;    ///< HA device_class, or nullptr
  const char *entity_category; ///< "diagnostic", or nullptr
} mqtt_entity_t;

/// The read-only entity table (sensors + binary_sensors + diagnostics).
const mqtt_entity_t *mqtt_entities(size_t *count);

/// Build the single JSON state document. Returns a malloc'd string (free with
/// cJSON's allocator via free()); caller owns it.
char *mqtt_build_state_json(const mqtt_state_t *s);

/// Build one entity's HA discovery config. Returns a malloc'd string.
char *mqtt_build_discovery_json(const mqtt_entity_t *e, const char *base_topic, const char *avail_topic,
                                const char *uid_prefix, const char *dev_name, const char *model, const char *fw);
