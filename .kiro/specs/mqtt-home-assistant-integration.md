# MQTT / Home Assistant Integration Spec

> **Status:** proposed. New Layer-4 (communication) subsystem living under
> [`components/rebel-espresso/src/comms/`](../../components/rebel-espresso/src/comms/),
> initialised from [`iot.cpp`](../../components/rebel-espresso/src/comms/iot.cpp),
> fully decoupled from the control/safety layers.

## Overview

Add a native **Home Assistant** integration over MQTT. The device connects to a
user-provided MQTT broker and **self-advertises** its entities using Home
Assistant **MQTT Discovery**, so sensors, binary sensors, diagnostics, and
controls appear automatically under a single HA device with no YAML.

This replicates the current web/HomeKit surface *and* exposes data that HomeKit
could not represent — most importantly the **water-probe voltage and corrosion
diagnostics** (see the
[Corrosion Monitoring spec](water-probe-corrosion-monitoring.md)).

A dedicated **MQTT configuration block** (enable/disable, broker endpoint, auth)
is persisted in NVS using the exact same `from_json`/`to_json` + web Config
section pattern as the other config blocks.

## Design principles (non-negotiable)

- MQTT runs entirely on **Layer 4** (main-thread comms, priority 3-5). It reads
  the process image and reacts to `MACHINE_EVENTS`; it **never** touches hardware
  actuators, blocks the control loop, or influences the safety gate.
- Only active when **WiFi is connected** and the feature is **enabled** in config.
- Commands received over MQTT go through the **same remote APIs** already used by
  the web/HomeKit paths (e.g. `power_active()`/`power_standby()`), so
  remote-vs-physical precedence and the safety gate are unchanged.

## Requirements

### R1: MQTT client
- Use the ESP-IDF built-in `mqtt` component (`esp-mqtt`) — no extra dependency.
- **Plain TCP only** (`mqtt://host:1883`) with **username/password (basic auth)**.
  No TLS for now — the target broker is not TLS-configured and TLS adds notable
  RAM/flash pressure. Revisit later if needed.
- Automatic reconnect with backoff; all network work off the control path.
- **Last Will & Testament (LWT)** on the availability topic so HA shows the
  device offline if it drops.

### R2: Connection configuration (NVS-backed, follows existing pattern)
- New `mqtt` config block with `from_json`/`to_json`, stable NVS keys, `_DEFAULT`
  constants, and a dedicated NVS store (e.g. `cfg.mqtt`). Fields (web form order):
  - `enabled` (bool, default false)
  - `broker_uri` (endpoint, e.g. `mqtt://host:1883`)
  - `username` (string)
  - `password` (string, write-only over the API — never returned in `to_json`)
  - `publish_interval_sec` (uint, default 5)
- **Derived, not configured** (removed from NVS + the config page): the MQTT
  `client_id`, the `base_topic`, and the `discovery_prefix`. Per HA best practice
  these come from the device identity + the fixed HA namespace:
  - `client_id`   = sanitised thing id
  - `base_topic`  = `<thing_type>/<thing_id>` (unique per device)
  - `discovery_prefix` = `homeassistant` (the fixed HA discovery namespace)
- Surfaced as an **MQTT** section on the web Config page, dispatched in
  `web_api_config` exactly like `boiler_temp`/`schedules`.
- Password handling mirrors the web-auth approach: accepted on write, stored in
  NVS, **redacted** in every read/export response.

### R3: Home Assistant MQTT Discovery
- On connect (and on config change), publish **retained** discovery configs to
  `<discovery_prefix>/<component>/<node_id>/<object_id>/config`.
- Every entity carries:
  - a stable `unique_id` (derived from thing id + object),
  - a shared **`device` block** (identifiers, name, model = `rebel-espresso`,
    manufacturer, sw_version, hw_version) so all entities group under one HA
    device,
  - an **availability** topic reference (R7),
  - `state_topic` / `command_topic` / `value_template` as appropriate.
- Discovery is republished on reconnect; entities are removed by publishing an
  empty retained config when the feature is disabled.

### R4: Entity set
Grouped by HA component and `entity_category`:

- **`climate`** (the thermostat, replicated from HomeKit — carries both power and
  brew-setpoint control):
  - Brew head: `current_temperature` = brew-head temp, target temperature = brew
    setpoint (adjustable, min/max/step matching the existing web control).
  - Modes **off / heat** carry machine power: `off` = standby, `heat` = active.
    There is no separate power switch — it would be redundant.
- **`sensor`** (primary):
  - Boiler temperature, boiler setpoint, brew-head temperature, boiler duty %,
    boiler water-level mV.
- **`binary_sensor`**:
  - Power active, brewing, steam, descale, refill active, refill error.
- **`sensor` / `binary_sensor` (`entity_category: diagnostic`)**:
  - **Probe voltage (median)** and **corrosion status** + baseline / thresholds
    (from the corrosion spec),
  - Boot count, crash count, free heap, min heap, uptime, WiFi RSSI,
    firmware version, IDF version, hardware revision.
- **Controls**:
  - Power + brew setpoint — both provided by the `climate` entity (off/heat mode
    and target temperature). No standalone power switch.
  - `button` — probe **Calibrate** (ties into the corrosion Calibrate action).

### R5: State publishing
- Publish **one compact JSON state document** to a single state topic
  (`<base_topic>/state`); every entity's discovery config selects its field via a
  `value_template`. One publish covers all read entities — minimal broker chatter.
- Cadence:
  - periodic on the Layer-4 poll / `TICK` at `publish_interval_sec`,
  - **plus on-change** for discrete events (power, brew, steam, descale, refill,
    corrosion status) driven off `MACHINE_EVENTS`.
- QoS 0 for the periodic state; QoS 1 + retain for availability.

### R6: Command handling
- Subscribe to `<base_topic>/cmd/#` (or per-control command topics).
- Map commands to the existing remote APIs:
  - climate mode (off/heat) → `power_standby()` / `power_active()`,
  - brew setpoint (climate target) → the same setter used by the web control API,
  - Calibrate button → the corrosion Calibrate action.
- Validate/clamp all inputs at the boundary; publish the resulting state back so
  HA reflects the accepted value.

### R7: Availability / LWT
- Retained availability topic `<base_topic>/availability` with `online` on
  connect and an LWT payload `offline` registered with the broker.
- All discovery configs reference this availability topic so entities grey out in
  HA when the device is offline.

### R8: Lifecycle & decoupling
- Initialised from `iot_init()` after connectivity is up; started only when
  `enabled` and WiFi connected; cleanly stopped/restarted on config change or
  WiFi loss.
- A single Layer-4 task/loop (or the existing IoT loop) owns the client; it reads
  the process image snapshot and event notifications only.

### R9: Security
- **Basic auth (username/password)** over plain TCP. No TLS for now (broker not
  TLS-configured; TLS adds significant RAM/flash). May revisit later.
- Credentials stored in NVS; **never** logged or returned by any API/export.
- Note secrets live in NVS plaintext (same as existing WiFi/identity material)
  and travel in the clear on the LAN without TLS; keep them out of telemetry.

### R10: Flash / memory budget
- `esp-mqtt` is already part of IDF; plain-TCP (no TLS) keeps RAM pressure low.
  Incremental app cost is the client glue + discovery/state serialisation (cJSON
  already linked). Target a few tens of KB.
- No impact when disabled (default). Verify the firmware still fits the OTA
  partition (2000 KB) after integration.

### R11: Hardware scope & testing
- Hardware revision 2 only.
- Pure serialisation logic (discovery-config builder, state-document builder,
  command parser/clamp) is **unit-tested** in `test_app` without a broker; the
  network client itself is validated on hardware against the user's broker.

## Design Decisions

- **Discovery-based, zero-config in HA.** Self-advertising avoids any HA YAML and
  keeps the device the single source of truth for its entities.
- **One JSON state topic + `value_template`** rather than one topic per entity —
  a single publish updates everything, minimal broker chatter, still HA-native.
- **`climate` entity replicates the thermostat** and provides brew-setpoint
  control for free (target temperature), so the only extra control is a Calibrate
  button (plus the power switch).
- **Reuse existing remote APIs for commands** so MQTT, web, and HomeKit share one
  command path and one precedence/safety model.
- **Plain TCP + basic auth.** No TLS for now to keep RAM/flash low; the LAN broker
  is not TLS-configured. Revisit if remote/off-LAN access is needed.
- **Coexists with HomeKit.** MQTT is additive; HomeKit (port 80) and the web
  server (port 8080) are unchanged. Users can enable either/both.
- **Config-block parity.** The `mqtt` section behaves like every other config
  block (JSON round-trip, NVS, web Config page, reset-to-defaults), so it needs
  no new patterns — only a new section.
- **Password redaction** follows the web-auth convention (write-accepted,
  read-redacted, excluded from config export/import).

## Open Questions
_All resolved for the initial implementation: one JSON state topic; controls =
power switch + brew setpoint (via the climate entity) + Calibrate button;
client-id/base-topic derived from `thing_id`; plain TCP + basic auth (no TLS)._

## Suggested Milestones
- **M1** — `mqtt` config block (NVS + web section, redacted password) and client
  connect/avail/LWT; no entities yet.
- **M2** — Discovery + read-only entities (sensors, binary_sensors) mirroring the
  web status.
- **M3** — Diagnostics entities incl. probe voltage + corrosion status.
- **M4** — Controls (power switch, brew setpoint, buttons) via existing remote
  APIs, with round-trip state confirmation.
