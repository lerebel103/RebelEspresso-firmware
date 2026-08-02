# Agent Rules

Rules for any AI agent working on this codebase.

## Git Safety

- Never push to any remote branch without explicit user permission.
- Never perform mutable git operations (force push, reset --hard, rebase, branch -D, amend pushed commits) without explicit user permission.
- Commits to the local working branch are allowed.
- Creating new local branches is allowed.

## Flash/Partition Awareness

- The ESP32 partition table defines hard limits on firmware binary size.
- The firmware binary must fit within the OTA partition size (currently 2000KB / 2MB per slot).
- Always verify binary size against partition limits before claiming a build is successful.
- The web UI (webapp/index.html) is gzipped and embedded in the firmware binary at build time. OTA updates the web UI and firmware together.

## Build Verification

- After making code changes, always verify the build succeeds before presenting the result.
- Run `make test` when changes affect testable logic (PID, NVS, JSON, events).
- Run `make lint` when modifying C/C++ source files.

## Pre-Commit Gate (mandatory before every commit and push)

Before EVERY `git commit`, run all three in order:
1. `make format` — auto-format all source files
2. `make build` — verify firmware compiles and check binary size
3. `make test` — verify all unit tests pass

If any step fails, fix the issue before committing. Never push code that hasn't passed all three steps. This is non-negotiable — CI will reject the PR otherwise.

## Code Style

- Follow the project's `.clang-format` and `.clang-tidy` configuration.
- Use ESP-IDF conventions for component structure, naming, and error handling.
- All new C/C++ code must pass `make lint` and `make format-check` before committing.

## Architecture

### Build & Deployment

- Build runs inside Docker (`make build`). Flash and monitor run on the host.
- The web interface backend is in `components/rebel-espresso/src/hw/r2/src/webserver/`.
- Config structs use `from_json()`/`to_json()` for serialization — maintain this pattern.
- All NVS keys must remain stable for backward compatibility with existing devices.
- Web server runs on port 8080 (HomeKit uses port 80).
- Hardware revision 2 is the active target (`HW_REVISION=2`).

### Design Principles

This firmware actuates heater SSRs, solenoids, and pumps on a real coffee machine. The following principles are non-negotiable:

1. **Safety first** — The firmware drives critical components (heater SSRs, solenoid valves, pumps). Every code path must ensure that failures result in safe states (heaters OFF, solenoids closed). Defensive checks must gate every actuation: sensor faults, out-of-range readings, low water, and communication failures must all force outputs to zero before anything else.

2. **Dedicated real-time control loop** — The main control loop (`process_loop`) is solely responsible for reading sensors and driving machine peripherals on its allocated tick cycle. No networking, display, or I/O-heavy work may execute in this context. Nothing may block or starve this loop.

3. **Watchdog-protected execution** — The control loop task is enrolled in the ESP Task Watchdog (2 s timeout, panic on expiry). If the loop stalls or fails to complete within the deadline, the watchdog triggers a system restart to return to a known-safe state.

4. **Event-driven secondary communication** — All non-critical work (TFT display updates, web server requests, HomeKit, cloud telemetry, OTA) runs on a secondary event loop that is fully decoupled from the control loop. The ESP event system (`MACHINE_EVENTS`) is the communication backbone — subsystems post events and listeners react asynchronously. This guarantees that a slow network response or a display redraw can never interfere with PID control or safety interlocks.

### Dual-Loop Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        app_main (main/main.cpp)                 │
│  Creates ESP event loop, inits connectivity + controller,       │
│  then enters controller_enter_loop() (never returns)            │
└──────────────────────────────┬──────────────────────────────────┘
                               │
             ┌─────────────────┴─────────────────┐
             ▼                                   ▼
┌──────────────────────────┐        ┌──────────────────────────────┐
│   CONTROL LOOP (1 Hz)    │        │   IOT / COMMUNICATION LOOP   │
│   (FreeRTOS task, pri 7) │        │   (main thread, ~1 Hz)       │
│                          │        │                              │
│  • HW timer ISR fires    │        │  • Web server lifecycle      │
│    every 1 s, gives      │        │  • OTA rollback validation   │
│    semaphore             │        │  • HomeKit                   │
│  • Task wakes, reads     │        │  • Future: cloud telemetry   │
│    RTD sensors           │  TICK  │                              │
│  • Posts TICK event ─────┼───────►│  Listeners react to events:  │
│  • Resets WDT (2 s)      │        │  display, schedules, etc.    │
│                          │        │                              │
│  Watchdog: panic if      │        │  Not time-critical; may      │
│  tick doesn't complete   │        │  block on network/flash      │
│  within 2 s              │        │  without affecting control   │
└──────────────────────────┘        └──────────────────────────────┘
```

**Control loop** (`process_loop.cpp`):
- A hardware GP timer fires an ISR every 1 second.
- The ISR gives a binary semaphore; the `process_loop` task (priority 7) wakes and executes:
  1. Read all RTD temperature sensors via SPI.
  2. Dispatch readings to subsystem handlers (PID boiler control, brew head temp).
  3. Post a `TICK` event so downstream listeners can react.
  4. Reset the task watchdog timer.
- Enrolled in `esp_task_wdt` with a 2 s timeout and `trigger_panic = true`.

**IoT / communication loop** (`iot.cpp`):
- Runs on the main thread after `controller_enter_loop()`.
- Manages web server start (after WiFi connects or AP activates), OTA self-test, and HomeKit.
- Polls at ~1 Hz with `vTaskDelay` — never touches hardware actuators directly.

### Event System

The ESP-IDF event loop (`MACHINE_EVENTS`) is the backbone for inter-component communication:

| Event | Posted by | Consumed by |
|-------|-----------|-------------|
| `TICK` | process_loop (every 1 s) | power, boiler_refill, brew, schedules, display |
| `POWER_STANDBY` | power | process_loop (relay off), boiler_temp (SSR off), display |
| `POWER_ACTIVE` | power | process_loop (relay on), display |
| `BREW_STARTED/STOPPED` | brew | display, metrics |
| `BOILER_REFILL_*` | boiler_refill | display, metrics |

A separate `status_event_group` (FreeRTOS EventGroup) provides fast bitwise state queries for real-time decisions:
- `POWER_ON_BIT` — machine is active
- `BOILER_LEVEL_OK_BIT` — water level safe for heating
- `DESCALE_MODE_BIT` — maintenance mode, heaters inhibited
- `WIFI_CONNECTED_BIT` / `WIFI_AP_ACTIVE_BIT` — network state

### Safety Mechanisms

| Mechanism | Implementation |
|-----------|---------------|
| Sensor fault → heater off | `boiler_temp_process()` forces SSR duty to 0 on any RTD error before doing anything else |
| Out-of-range temp → heater off | Readings >140 °C or <0 °C immediately kill SSR output |
| Over-temp threshold → heater off | PID result flagged `is_over_threshold` forces duty to 0 |
| Low water → heater off | `BOILER_LEVEL_OK_BIT` checked before every PID cycle |
| Sustained sensor errors → restart | Configurable timeout (`temp_error_restart_time_sec`) triggers `esp_restart()` |
| Standby → all outputs off | `POWER_STANDBY` event forces SSR and relay outputs to 0 |
| Init → outputs forced LOW | `out_signals_init()` writes all IO expander pins LOW before any logic runs |
| Watchdog → panic restart | 2 s task WDT on control loop; failure causes immediate restart to safe state |
| OTA rollback | New firmware must pass self-test (WiFi + web server up) before being marked valid; failure causes bootloader rollback |
| SSR duty clamping | `ssr_ctrl_set_duty()` trims input to [0, 100] regardless of caller |

### Component Map

```
components/
├── rebel-espresso/             ← Main application logic
│   ├── src/hw/base/            ← Hardware-independent: PID, state machines, control loops
│   │   ├── process_loop.cpp    ← Real-time control loop (timer ISR + WDT)
│   │   ├── boiler_temp.cpp     ← PID heater control + safety interlocks
│   │   ├── power.cpp           ← Standby/active state machine
│   │   ├── brew.cpp            ← Brew shot control
│   │   ├── boiler_refill.cpp   ← Water level management
│   │   ├── iot.cpp             ← Communication loop (web, OTA, HomeKit)
│   │   └── controller.cpp      ← Init orchestration, enters event loop
│   ├── src/hw/r2/src/          ← Hardware revision 2 specifics
│   │   ├── hw_config.h         ← Pin assignments, I2C/SPI addresses
│   │   ├── hw_specs.cpp        ← Sensor dispatch, HW-specific init
│   │   ├── out_signals.c       ← I2C IO expander relay control
│   │   └── webserver/          ← HTTP API handlers (port 8080)
│   ├── src/utils/              ← PID algorithm, state machine template
│   └── src/sys/                ← NVS abstraction
├── esp-connectivity/           ← WiFi STA/AP, captive portal, SNTP, identity
├── esp-ssr-controller/         ← Zero-cross SSR duty-cycle driver (mains-synced)
├── ESP32-MAX31865/             ← RTD temperature sensor SPI driver
├── esp-homekit-sdk/            ← Apple HomeKit (submodule)
└── tft-driver/                 ← ST7796 TFT display driver
```
