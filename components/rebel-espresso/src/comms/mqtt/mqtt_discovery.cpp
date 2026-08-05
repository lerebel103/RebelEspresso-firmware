#include "mqtt_discovery.h"

#include <cJSON.h>
#include <cstdio>
#include <cstring>

// Read-only entity set. Booleans are rendered ON/OFF by their value_template so
// HA binary_sensors work with the default payload_on/payload_off.
static const mqtt_entity_t s_entities[] = {
    // component,       object_id,        name,                value_template, unit,  device_class,   entity_category
    {"sensor", "boiler_temp", "Boiler Temperature", "{{ value_json.boiler_temp }}", "\u00b0C", "temperature", nullptr},
    {"sensor", "boiler_setpoint", "Boiler Setpoint", "{{ value_json.boiler_setpoint }}", "\u00b0C", "temperature",
     nullptr},
    {"sensor", "brew_temp", "Brew Temperature", "{{ value_json.brew_temp }}", "\u00b0C", "temperature", nullptr},
    {"sensor", "brew_setpoint", "Brew Setpoint", "{{ value_json.brew_setpoint }}", "\u00b0C", "temperature", nullptr},
    {"sensor", "boiler_duty", "Boiler Duty", "{{ value_json.boiler_duty }}", "%", nullptr, nullptr},
    {"binary_sensor", "power", "Power", "{{ 'ON' if value_json.power else 'OFF' }}", nullptr, "power", nullptr},
    {"binary_sensor", "brewing", "Brewing", "{{ 'ON' if value_json.brewing else 'OFF' }}", nullptr, "running", nullptr},
    {"binary_sensor", "steam", "Steam", "{{ 'ON' if value_json.steam else 'OFF' }}", nullptr, nullptr, nullptr},
    {"binary_sensor", "descale", "Descale", "{{ 'ON' if value_json.descale else 'OFF' }}", nullptr, nullptr, nullptr},
    {"binary_sensor", "refill_active", "Refilling", "{{ 'ON' if value_json.refill_active else 'OFF' }}", nullptr,
     "running", nullptr},
    {"binary_sensor", "refill_error", "Refill Error", "{{ 'ON' if value_json.refill_error else 'OFF' }}", nullptr,
     "problem", nullptr},
    // Diagnostics (probe health + water level)
    {"sensor", "probe_voltage", "Probe Voltage", "{{ value_json.probe_mv }}", "mV", "voltage", "diagnostic"},
    {"sensor", "water_level", "Water Level", "{{ value_json.water_level_mv }}", "mV", "voltage", "diagnostic"},
    {"sensor", "corrosion_status", "Probe Corrosion",
     "{{ ['OK','Service soon','Fault'][value_json.corrosion_status] }}", nullptr, nullptr, "diagnostic"},
};

const mqtt_entity_t *mqtt_entities(size_t *count) {
  *count = sizeof(s_entities) / sizeof(s_entities[0]);
  return s_entities;
}

static void _add_device(cJSON *root, const char *uid_prefix, const char *dev_name, const char *model, const char *fw) {
  cJSON *device = cJSON_AddObjectToObject(root, "device");
  cJSON *ids = cJSON_AddArrayToObject(device, "identifiers");
  cJSON_AddItemToArray(ids, cJSON_CreateString(uid_prefix));
  cJSON_AddStringToObject(device, "name", dev_name);
  cJSON_AddStringToObject(device, "model", model);
  cJSON_AddStringToObject(device, "manufacturer", "RebelEspresso");
  cJSON_AddStringToObject(device, "sw_version", fw);
}

char *mqtt_build_state_json(const mqtt_state_t *s) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "boiler_temp", s->boiler_temp);
  cJSON_AddNumberToObject(root, "boiler_setpoint", s->boiler_setpoint);
  cJSON_AddNumberToObject(root, "brew_temp", s->brew_temp);
  cJSON_AddNumberToObject(root, "brew_setpoint", s->brew_setpoint);
  cJSON_AddNumberToObject(root, "boiler_duty", s->boiler_duty);
  cJSON_AddNumberToObject(root, "water_level_mv", s->water_level_mv);
  cJSON_AddNumberToObject(root, "probe_mv", s->probe_mv);
  cJSON_AddNumberToObject(root, "corrosion_status", s->corrosion_status);
  cJSON_AddBoolToObject(root, "power", s->power);
  cJSON_AddBoolToObject(root, "brewing", s->brewing);
  cJSON_AddBoolToObject(root, "steam", s->steam);
  cJSON_AddBoolToObject(root, "descale", s->descale);
  cJSON_AddBoolToObject(root, "refill_active", s->refill_active);
  cJSON_AddBoolToObject(root, "refill_error", s->refill_error);

  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return out;
}

char *mqtt_build_discovery_json(const mqtt_entity_t *e, const char *base_topic, const char *avail_topic,
                                const char *uid_prefix, const char *dev_name, const char *model, const char *fw) {
  cJSON *root = cJSON_CreateObject();

  cJSON_AddStringToObject(root, "name", e->name);

  char uid[96];
  snprintf(uid, sizeof(uid), "%s_%s", uid_prefix, e->object_id);
  cJSON_AddStringToObject(root, "unique_id", uid);

  char state_topic[128];
  snprintf(state_topic, sizeof(state_topic), "%s/state", base_topic);
  cJSON_AddStringToObject(root, "state_topic", state_topic);
  cJSON_AddStringToObject(root, "availability_topic", avail_topic);
  cJSON_AddStringToObject(root, "value_template", e->value_template);

  if (e->unit) {
    cJSON_AddStringToObject(root, "unit_of_measurement", e->unit);
  }
  if (e->device_class) {
    cJSON_AddStringToObject(root, "device_class", e->device_class);
  }
  if (e->entity_category) {
    cJSON_AddStringToObject(root, "entity_category", e->entity_category);
  }

  // Shared device block so all entities group under one HA device.
  cJSON *device = cJSON_AddObjectToObject(root, "device");
  cJSON *ids = cJSON_AddArrayToObject(device, "identifiers");
  cJSON_AddItemToArray(ids, cJSON_CreateString(uid_prefix));
  cJSON_AddStringToObject(device, "name", dev_name);
  cJSON_AddStringToObject(device, "model", model);
  cJSON_AddStringToObject(device, "manufacturer", "RebelEspresso");
  cJSON_AddStringToObject(device, "sw_version", fw);

  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return out;
}

char *mqtt_build_brew_climate_json(const char *base_topic, const char *avail_topic, const char *uid_prefix,
                                   const char *dev_name, const char *model, const char *fw) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", "Brew");

  char uid[96];
  snprintf(uid, sizeof(uid), "%s_brew", uid_prefix);
  cJSON_AddStringToObject(root, "unique_id", uid);

  char state[160];
  snprintf(state, sizeof(state), "%s/state", base_topic);
  cJSON_AddStringToObject(root, "current_temperature_topic", state);
  cJSON_AddStringToObject(root, "current_temperature_template", "{{ value_json.brew_temp }}");
  cJSON_AddStringToObject(root, "temperature_state_topic", state);
  cJSON_AddStringToObject(root, "temperature_state_template", "{{ value_json.brew_setpoint }}");

  char cmd[160];
  snprintf(cmd, sizeof(cmd), "%s/cmd/brew_setpoint", base_topic);
  cJSON_AddStringToObject(root, "temperature_command_topic", cmd);

  cJSON_AddNumberToObject(root, "min_temp", 85);
  cJSON_AddNumberToObject(root, "max_temp", 100);
  cJSON_AddNumberToObject(root, "temp_step", 0.5);
  cJSON_AddStringToObject(root, "temperature_unit", "C");

  // Power is combined into the thermostat: off = standby, heat = active.
  cJSON *modes = cJSON_AddArrayToObject(root, "modes");
  cJSON_AddItemToArray(modes, cJSON_CreateString("off"));
  cJSON_AddItemToArray(modes, cJSON_CreateString("heat"));
  cJSON_AddStringToObject(root, "mode_state_topic", state);
  cJSON_AddStringToObject(root, "mode_state_template", "{{ 'heat' if value_json.power else 'off' }}");

  char mcmd[160];
  snprintf(mcmd, sizeof(mcmd), "%s/cmd/mode", base_topic);
  cJSON_AddStringToObject(root, "mode_command_topic", mcmd);

  cJSON_AddStringToObject(root, "availability_topic", avail_topic);
  _add_device(root, uid_prefix, dev_name, model, fw);

  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return out;
}

char *mqtt_build_calibrate_button_json(const char *base_topic, const char *avail_topic, const char *uid_prefix,
                                       const char *dev_name, const char *model, const char *fw) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", "Calibrate Probe");

  char uid[96];
  snprintf(uid, sizeof(uid), "%s_calibrate", uid_prefix);
  cJSON_AddStringToObject(root, "unique_id", uid);

  char cmd[160];
  snprintf(cmd, sizeof(cmd), "%s/cmd/calibrate", base_topic);
  cJSON_AddStringToObject(root, "command_topic", cmd);
  cJSON_AddStringToObject(root, "payload_press", "PRESS");
  cJSON_AddStringToObject(root, "availability_topic", avail_topic);
  cJSON_AddStringToObject(root, "entity_category", "config");
  _add_device(root, uid_prefix, dev_name, model, fw);

  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return out;
}
