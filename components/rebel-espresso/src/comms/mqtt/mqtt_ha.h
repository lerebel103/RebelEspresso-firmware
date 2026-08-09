#pragma once

/**
 * MQTT / Home Assistant client (Layer 4). Plain TCP + basic auth, no TLS.
 * Decoupled from control/safety — reads config + process image only.
 */

/// Load MQTT config from NVS. Call once at init.
void mqtt_ha_init();

/// Called ~1 Hz from the IoT loop: starts the client when enabled + WiFi is up,
/// stops it when disabled. esp-mqtt owns reconnect once started.
void mqtt_ha_service();

/// Publish offline availability, stop and destroy the client.
void mqtt_ha_stop();

/// True when MQTT is enabled in config.
bool mqtt_ha_is_enabled();

/// True when the client currently has a live broker connection.
bool mqtt_ha_is_connected();
