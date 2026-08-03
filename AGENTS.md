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

### Documentation Map

Where to find things (keep these in sync with the code):

| Looking for… | Read |
|--------------|------|
| Architectural truth (layers, tasks, timing) | [`.kiro/specs/realtime-io-architecture.md`](.kiro/specs/realtime-io-architecture.md) |
| Parity/safety audit + outstanding gaps | [`.kiro/specs/realtime-io-parity-audit.md`](.kiro/specs/realtime-io-parity-audit.md) |
| Repository layout audit + target structure | [`.kiro/specs/realtime-io-layout-audit.md`](.kiro/specs/realtime-io-layout-audit.md) |
| Required manual hardware validation | [`docs/testing/manual-hardware-validation.md`](docs/testing/manual-hardware-validation.md) |
| Merge-gate + validation policy | this file (Pre-Commit Gate, Required Hardware Validation) |

### Build & Deployment

- Build runs inside Docker (`make build`). Flash and monitor run on the host.
- The web interface backend is in `components/rebel-espresso/src/hw/r2/src/webserver/`.
- Config structs use `from_json()`/`to_json()` for serialization — maintain this pattern.
- All NVS keys must remain stable for backward compatibility with existing devices.
- Web server runs on port 8080 (HomeKit uses port 80).
- Hardware revision 2 is the active target (`HW_REVISION=2`).

### Required Hardware Validation

Changes that affect safety-critical real-time behavior must be validated on physical hardware
before being considered ready to merge. This includes, but is not limited to:
- heater / SSR control
- relay and solenoid actuation
- power state transitions
- boiler refill logic
- RTD / sensor acquisition
- remote-vs-physical command precedence
- timing, latency, and watchdog behavior

Where a behavior cannot be fully automated in CI or unit tests, it must be verified manually
on a physical test board and recorded in:

`docs/testing/manual-hardware-validation.md`

This validation step is required for firmware evolutions that can affect safety, timing, or
machine state. If hardware validation cannot be completed, the limitation must be called out
explicitly in the PR description or audit notes.

### Design Principles

This firmware actuates heater SSRs, solenoids, and pumps on a real coffee machine. The following principles are non-negotiable:

1. **Safety first** — The firmware drives critical components (heater SSRs, solenoid valves, pumps). Every code path must ensure that failures result in safe states (heaters OFF, solenoids closed). Defensive checks must gate every actuation: sensor faults, out-of-range readings, low water, and communication failures must all force outputs to zero before anything else.

2. **Dedicated real-time I/O and control layers** — Physical I/O is handled by a high-priority I/O scan task (20 ms) that reads switches, drives the refill state machine, and applies every output through a single safety gate. A strict 1 Hz control loop (`process_loop`) runs the PID and writes desired duties. A sensor task owns RTD/water-level acquisition. No networking, display, or I/O-heavy work may execute in these contexts, and nothing may block or starve them. See [`.kiro/specs/realtime-io-architecture.md`](.kiro/specs/realtime-io-architecture.md) and [`.kiro/specs/realtime-io-parity-audit.md`](.kiro/specs/realtime-io-parity-audit.md).

3. **Watchdog-protected execution** — The control loop task is enrolled in the ESP Task Watchdog (2 s timeout, panic on expiry). If the loop stalls or fails to complete within the deadline, the watchdog triggers a system restart to return to a known-safe state.

4. **Event-driven secondary communication** — All non-critical work (TFT display updates, web server requests, HomeKit, cloud telemetry, OTA) runs on a secondary event loop that is fully decoupled from the control loop. The ESP event system (`MACHINE_EVENTS`) is the communication backbone — subsystems post events and listeners react asynchronously. This guarantees that a slow network response or a display redraw can never interfere with PID control or safety interlocks.

### Layered Scan Architecture

The firmware uses a four-layer, PLC-inspired scan architecture. All layers exchange
state through a single shared `process_image_t` (one writer per field). See the
[architecture spec](.kiro/specs/realtime-io-architecture.md) and
[parity/safety audit](.kiro/specs/realtime-io-parity-audit.md) for the full design,
ownership model, and safety gate.

```
┌─────────────────────────────────────────────────────────────────┐
│                        app_main (main/main.cpp)                 │
│  Creates ESP event loop, inits connectivity + controller,       │
│  then enters controller_enter_loop() (never returns)            │
└──────────────────────────────┬──────────────────────────────────┘
                               │  all layers read/write process_image_t
   ┌───────────────┬───────────┴───────────┬────────────────────┐
   ▼               ▼                       ▼                    ▼
┌──────────┐  ┌──────────┐  ┌──────────────────┐  ┌──────────────────────┐
│ I/O SCAN │  │ SENSOR   │  │  CONTROL LOOP    │  │  IOT / COMMUNICATION │
│ 20 ms    │  │ TASK     │  │  1 Hz (HW timer) │  │  main thread ~1 Hz   │
│ pri 8    │  │ ~150 ms  │  │  pri 7           │  │  pri 3-5             │
│          │  │ pri 6    │  │                  │  │                      │
│ • debounce│ │ • RTDs   │  │ • read temps     │  │ • web server         │
│   switches│ │   (SPI)  │  │   from image     │  │ • OTA rollback       │
│ • refill  │ │ • water  │  │ • run PID →      │  │ • HomeKit            │
│   state   │ │   level  │  │   ssr_boiler_duty│  │ • TFT display        │
│ • brew    │ │ • writes │  │ • brew temp trim │  │                      │
│   edges   │ │   image  │  │ • post TICK ─────┼──► reacts to events    │
│ • SAFETY  │ │          │  │ • reset 2s WDT   │  │                      │
│   GATE +  │ │          │  │                  │  │  never touches HW    │
│   write   │ │          │  │                  │  │  actuators directly  │
│   outputs │ │          │  │                  │  │                      │
│ • shared  │ │          │  │                  │  │                      │
│   2s WDT  │ │          │  │                  │  │                      │
└──────────┘  └──────────┘  └──────────────────┘  └──────────────────────┘
```

**I/O scan** (`io_scan.cpp`, priority 8, 20 ms):
- Reads and software-debounces all switch GPIOs (power, brew, steam) — no edge ISRs.
- Drives the boiler refill state machine and tracks brew on/off edges.
- Applies the **safety gate** (`apply_outputs`) and writes every output every cycle
  (SSR via `boiler_temp_apply_hw_duty`, relays via `out_signals_set_level`).
- Syncs `status_event_group` bits from the process image for Layer 4 consumers.
- Enrolled in the task WDT.

**Sensor task** (`sensor_task.cpp`, priority 6, ~150 ms):
- Owns RTD acquisition via `rtds_update()` and water-level ADC reads.
- Writes temperatures + faults and `water_level_ok` into the process image.
- Water probe voltage is pulsed only when powered on and not descaling (corrosion guard).

**Control loop** (`process_loop.cpp`, priority 7, strict 1 Hz):
- A hardware GP timer ISR gives a binary semaphore every 1 s; the task wakes and:
  1. Reads the latest temperatures from the process image (no direct SPI).
  2. Dispatches to PID boiler control and brew-head trim.
  3. Writes `ssr_boiler_duty` to the process image (does not touch the SSR directly).
  4. Posts a `TICK` event for Layer 4 consumers.
  5. Resets the 2 s task watchdog (`trigger_panic = true`).

**IoT / communication loop** (`iot.cpp`):
- Runs on the main thread after `controller_enter_loop()`.
- Manages web server start, OTA self-test, and HomeKit.
- Polls at ~1 Hz with `vTaskDelay` — never touches hardware actuators directly.

### Event System

The ESP-IDF event loop (`MACHINE_EVENTS`) is the backbone for inter-component communication:

| Event | Posted by | Consumed by |
|-------|-----------|-------------|
| `TICK` | process_loop (every 1 s) | boiler_temp (stats save), schedules, display |
| `POWER_STANDBY` | io_scan (on power-off edge/remote) | boiler_temp (PID reset), brew, display |
| `POWER_ACTIVE` | io_scan (on power-on edge/remote) | boiler_temp (PID resume), brew (descale count), display |
| `BREW_STARTED/STOPPED` | io_scan (brew edge) | brew (stats), display, metrics |
| `BOILER_REFILL_*` | io_scan (refill state change) | display, metrics |

A separate `status_event_group` (FreeRTOS EventGroup) provides fast bitwise state queries for
real-time decisions. The I/O scan projects these bits from the process image every cycle:
- `POWER_ON_BIT` — machine is active
- `BOILER_LEVEL_OK_BIT` — water level safe for heating
- `DESCALE_MODE_BIT` — maintenance mode, heaters inhibited
- `WIFI_CONNECTED_BIT` / `WIFI_AP_ACTIVE_BIT` — network state

### Safety Mechanisms

| Mechanism | Implementation |
|-----------|---------------|
| Final output safety gate | `io_scan::apply_outputs()` applies every heater-inhibit condition every 20 ms cycle and is the ONLY path that drives the SSR/relays. No control-path caller writes hardware directly. |
| Sensor fault → heater off | `apply_outputs` cuts SSR on boiler RTD fault byte; `boiler_temp_process()` also forces duty 0 on any RTD error |
| Out-of-range temp → heater off | Readings >140 °C or <0 °C kill SSR in `boiler_temp_process()`; `apply_outputs` independently cuts SSR above 140 °C (defense-in-depth) |
| Over-temp threshold → heater off | PID result flagged `is_over_threshold` forces duty to 0 |
| Low water → heater off | `apply_outputs` cuts SSR when `!water_level_ok`; also checked in `boiler_temp_process()` |
| Refill error → heater off | `apply_outputs` cuts SSR when `refill_state == REFILL_STATE_ERROR` (latched until power cycle) |
| Descale mode → heater off | `apply_outputs` cuts SSR when `descale_mode`; also checked in `boiler_temp_process()` |
| Sustained sensor errors → restart | Configurable timeout (`temp_error_restart_time_sec`) triggers `esp_restart()` |
| Standby → all outputs off | `apply_outputs` forces SSR + all relays to 0 within one 20 ms scan when `!power_on` |
| Init → outputs forced LOW | `process_image_init()` sets all outputs OFF and all sensors faulted before any task starts; `out_signals_init()` writes IO expander pins LOW |
| Watchdog → panic restart | 2 s task WDT on control loop + I/O scan; failure causes immediate restart to safe state |
| OTA rollback | New firmware must pass self-test (WiFi + web server up) before being marked valid; failure causes bootloader rollback |
| SSR duty clamping | `ssr_ctrl_set_duty()` trims input to [0, 100] regardless of caller |
| Water probe corrosion guard | Water level probe voltage is ONLY applied during active operation (not in standby or descale). The probe enable GPIO is pulsed momentarily per read to minimise galvanic corrosion of the electrodes. |

### Component Map

```
components/
├── rebel-espresso/             ← Main application logic
│   ├── src/control/            ← Shared runtime/control logic (board-independent)
│   │   ├── process_image.h/.cpp← Shared state bridging all layers (one writer per field)
│   │   ├── io_scan.cpp         ← Layer 1: 20 ms I/O scan, debounce, refill, safety gate
│   │   ├── sensor_task.cpp     ← Layer 2: RTD + water-level acquisition
│   │   ├── process_loop.cpp    ← Layer 3: 1 Hz PID control loop (timer ISR + WDT)
│   │   ├── boiler_temp.cpp     ← PID heater control + safety interlocks
│   │   ├── power.cpp           ← Remote standby/active API (switch polled by io_scan)
│   │   ├── brew.cpp            ← Brew shot statistics (edges handled by io_scan)
│   │   ├── boiler_refill_states.cpp ← Refill state machine (driven by io_scan)
│   │   ├── iot.cpp             ← Communication loop (web, OTA, HomeKit)
│   │   └── controller.cpp      ← Init orchestration, enters event loop
│   ├── src/hw/r2/src/          ← R2 board-specific code (only supported board)
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
