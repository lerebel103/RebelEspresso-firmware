# Real-Time I/O Architecture Spec

## Overview

Refactor the firmware control architecture from a single 1Hz tick-driven loop (that polls GPIO inputs and actuates outputs through the event system) into a layered scan-cycle architecture inspired by industrial PLC design. The goal is to guarantee <40ms input-to-output latency for all safety-critical physical I/O (switches, relays, SSR) while preserving strict PID timing and keeping non-critical communication decoupled.

## Problem Statement

The current architecture has a fundamental latency issue:
- Power switch, brew switch, and water level are polled or event-driven at 1Hz (the TICK rate)
- When a switch toggles, there can be up to 1000ms before the corresponding output (pump, solenoid, SSR) reacts
- GPIO ISR edge detection has proven unreliable for consistent state tracking (missed edges, bounce issues)
- Quick on/off/on toggling is missed entirely because the event handler only runs once per second
- The brew switch has a dedicated ISR + task (2.5KB) that works for ON detection but has random delays for OFF

## Architecture

Four layers, each with a dedicated responsibility, priority, and timing guarantee:

### Layer 1: I/O Scan Task (20ms cycle, priority 8 — highest)

Responsibilities:
- Read all GPIO switch inputs every 20ms (power, brew, steam)
- Software debounce all inputs (no hardware ISRs for switch detection)
- Drive the boiler refill state machine (reads water_level_ok from process image)
- Track brew state transitions (on/off edges, brew duration)
- Apply safety overrides before writing outputs
- Write ALL physical outputs unconditionally every cycle (SSR duty, I2C relays)
- Reset its own 200ms WDT

Safety override rules (applied every cycle, non-negotiable):
- If !power_on → ALL outputs forced OFF
- If !water_level_ok → SSR duty forced to 0
- If refill_error → SSR duty forced to 0
- If descale_mode → SSR duty forced to 0

### Layer 2: ADC / Sensor Task (priority 6, free-running ~5-10Hz)

Responsibilities:
- Own the SPI bus (shared with TFT, accessed via ESP-IDF SPI device mutex)
- Read all RTD temperature channels from ADS124S08 sequentially
- Read water level ADC channel
- Write results to process image (temperatures, fault codes, water_level_mv, water_level_ok)
- Sleep between scan passes (~100-200ms)

### Layer 3: Control Loop (strict 1Hz hardware timer, priority 7)

Responsibilities:
- Wake on semaphore from timer ISR (exactly 1s interval)
- Read latest temperature + input state from process image
- Run PID algorithm → compute ssr_boiler_duty
- Run brew temperature trim logic
- Write desired output duties to process image
- Post TICK event for Layer 4 consumers
- Reset 2s WDT

### Layer 4: Communication (main thread, ~1Hz, priority 3-5)

Responsibilities:
- Web server, HomeKit, OTA, TFT display updates
- Read process image (snapshot) for status reporting
- Receive events for state change notifications
- Never touches hardware directly

## Process Image

A shared data structure that bridges all layers:

```c
struct process_image_t {
    // --- INPUTS (written by I/O Scan, read by Control Loop) ---
    bool     power_on;              // Debounced power switch
    bool     brew_on;               // Debounced brew switch
    bool     steam_on;              // Debounced steam switch
    bool     descale_mode;          // Set on power-on if brew switch held

    // --- SENSOR DATA (written by ADC Task, read by I/O Scan + Control Loop) ---
    measure_t temperatures[RTD_MAX_COUNT]; // Temperature + fault per channel
    double   water_level_mv;        // Raw ADC voltage
    bool     water_level_ok;        // Derived: mv <= threshold

    // --- OUTPUTS (written by Control Loop, read + applied by I/O Scan) ---
    int      ssr_boiler_duty;       // 0-100 from PID
    bool     pump_on;               // Desired pump relay state
    bool     refill_solenoid_on;    // Desired refill valve state
    bool     three_way_on;          // Desired 3-way valve state
    bool     aux_on;                // Auxiliary output

    // --- STATE (written by I/O Scan, read by all) ---
    RefillState_t refill_state;     // Current refill state machine state
    bool     brew_active;           // Brew in progress
    uint64_t brew_start_time_us;    // When current brew started
    uint64_t last_scan_time_us;     // Timestamp of last I/O scan completion
};
```

Ownership rules:
- Each field has exactly one writer (layer that owns it)
- Other layers read only
- Atomicity: I/O scan runs at highest priority so cannot be preempted mid-write by lower layers. Aligned word-size fields are inherently atomic on ESP32. For multi-field consistency (temperature + fault pair), use a sequence counter.

## Requirements

### R1: Introduce Process Image
- Create `process_image.h` with the shared struct definition
- Create a singleton accessor `process_image_t* process_image_get()`
- All layers interact with hardware state exclusively through the process image
- No direct gpio_get_level or ssr_ctrl_set_duty calls from the control loop

### R2: Implement I/O Scan Task
- Create `io_scan.cpp` / `io_scan.h` in `components/rebel-espresso/src/hw/base/`
- 20ms polling loop at FreeRTOS priority 8
- Read all switch GPIOs with software debounce (configurable debounce time, default 40ms = 2 consecutive same-state reads)
- Drive refill state machine every cycle
- Track brew on/off transitions with edge detection from debounced state
- Apply safety overrides per spec
- Write SSR duty via `ssr_ctrl_set_duty` every cycle
- Write relays via `out_signals_set_level` every cycle (unconditionally)
- Enroll in task WDT with 200ms timeout
- Stack allocation: 2048 bytes

### R3: Implement ADC Sensor Task
- Create `sensor_task.cpp` / `sensor_task.h` in `components/rebel-espresso/src/hw/base/`
- Owns RTD reads via existing `rtds_update()` mechanism
- Owns water level ADC read via existing `hw_specs_read_water_level_mv()`
- Writes results to process image
- Acquires SPI bus via ESP-IDF device-level locking (existing pattern)
- Free-running with ~100-200ms sleep between passes
- Priority 6, stack 3072 bytes

### R4: Refactor Control Loop
- `process_loop.cpp` retains its 1Hz timer ISR and task structure
- Remove direct RTD reads from the control loop (now done by ADC task)
- Read temperatures from process image instead
- `boiler_temp_process()` reads input state from process image (no more direct event group bit checks for power/water/descale)
- Write `ssr_boiler_duty` to process image (no more direct `ssr_ctrl_set_duty` calls)
- Retain WDT reset and TICK event posting

### R5: Remove Legacy Input Handling
- Remove `monitor_brew` task and brew GPIO ISR from `brew.cpp`
- Remove power switch polling from `power.cpp` TICK handler
- Remove the disconnected boiler_refill `_tick()` handler (state machine now in I/O scan)
- Remove MAX31865 component reference (hardware R1 only, no longer applicable)

### R6: Safety Guarantees
- I/O scan safety overrides must pass all existing safety interlock tests
- Error state in refill latches until power cycle (no auto-recovery)
- All outputs forced OFF on power standby within one scan cycle (20ms)
- SSR duty forced to 0 on any sensor fault, low water, or descale mode
- WDT on I/O scan (200ms) guarantees system restart if scan stalls

### R7: Test Coverage
- Existing safety interlock tests must continue to pass (adapted to exercise logic through process image)
- New tests for:
  - Process image field isolation (writers don't conflict)
  - I/O scan safety override logic (each override independently verified)
  - Debounce behavior (quick toggles filtered, stable state passes through)
  - Refill state machine driven at scan rate (timeout, error latch, power cycle recovery)
  - Brew edge detection (on→off and off→on transitions detected correctly)
  - Control loop reads from process image correctly

## Migration Strategy

Incremental, each step independently testable:

1. Introduce process image struct (pass-through, no behavior change)
2. Create I/O scan task with power switch + safety overrides
3. Move brew into I/O scan, remove monitor_brew task
4. Create ADC sensor task, decouple RTD reads from control loop
5. Move refill state machine into I/O scan
6. Wire control loop to read/write process image
7. Remove stale code (old event handlers, MAX31865, unused tasks)
8. Update AGENTS.md architecture documentation

## Constraints

- Firmware binary must remain within 2MB OTA partition
- Total heap usage must not exceed available RAM (monitor via Free Heap logging)
- PID timing must remain strict 1Hz (no jitter from SPI or I2C contention)
- SPI bus shared with TFT display — use ESP-IDF device mutex, never hold bus across a vTaskDelay
