# Real-Time I/O Architecture — Parity & Safety Audit

Companion to [`realtime-io-architecture.md`](realtime-io-architecture.md). This document
closes out the hardening task list: it maps every legacy behaviour to its new owner,
records the ownership model, and captures the audit findings for each safety-critical
runtime path.

Status legend:

- ✅ **preserved** — behaviour matches legacy, owner identified, path verified
- 🟡 **partially preserved** — works but has a documented gap or transitional leftover
- ❌ **not yet preserved** — regression or missing behaviour
- 🔁 **intentionally changed** — deliberate design change from legacy

---

## 1. Legacy-to-new parity matrix

| Behaviour | New owner (runtime path) | Reads from | Writes to | Status | Notes |
|-----------|--------------------------|------------|-----------|--------|-------|
| Physical power switch ON/OFF | `io_scan::scan_inputs` (20 ms) | `gpio_get_level(PIN_IN_SYS_EN)` debounced | `img->power_on`; posts `POWER_ACTIVE`/`POWER_STANDBY` | ✅ | Edge-detected; seeds initial state at boot. |
| Remote (HomeKit/HTTP) power ON/OFF | `power_active()` / `power_standby()` | — | `img->power_on` | ✅ | Writes process image directly; io_scan detects the change and fires events + refill restart. |
| Switch vs remote conflict | `io_scan::scan_inputs` edge vs direct write | `img->power_on` vs `s_prev_power_on` | `img->power_on` | ✅ | "Last transition wins" — see §3. |
| Descale mode entry | `io_scan::scan_inputs` on power-on if brew held | `s_brew_db.stable_state` | `img->descale_mode` | ✅ | Cleared on power-off. |
| Brew enable/disable | `io_scan::scan_inputs` brew edge | debounced `PIN_IN_BREW_EN` | `img->brew_active`, `pump_on`, `three_way_on`; posts `BREW_STARTED/STOPPED` | ✅ | Only starts when `power_on`. |
| Brew statistics (count, descale count) | `brew.cpp` event handlers | `BREW_STOPPED`/`POWER_ACTIVE` events | NVS | ✅ | Decoupled to Layer 4; ≥10 s shots counted. |
| Boiler refill state machine | `io_scan::scan_refill` (20 ms) drives `boiler_refill_states_process` | `img->water_level_ok` | `img->refill_state`, `refill_solenoid_on`, `pump_on`; posts `BOILER_REFILL_*` | ✅ | See §4. Not run in standby/descale. |
| Refill error latch | `boiler_refill_states.cpp` ERROR state | — | latched until `power_on` cycle | ✅ | `_state_error_process` is a no-op; cleared by `boiler_refill_states_power_on()`. |
| RTD acquisition | `sensor_task.cpp` (~150 ms) via `rtds_update` | SPI ADS124S08 | `img->temperatures[]` | ✅ | See §5. |
| Water level acquisition | `sensor_task.cpp` `_read_water_level` | ADC, probe pulsed | `img->water_level_mv`, `water_level_ok` | ✅ | Probe only energised when `power_on && !descale` (corrosion guard). |
| PID boiler control | `process_loop.cpp` (1 Hz) → `boiler_temp_process` | `img->temperatures[]` | `img->ssr_boiler_duty` | 🟡 | Reads power/water/descale via **event-group bits**, not the image directly — transitional leftover, see §9. |
| Brew-head temp trim | `boiler_temp_process` → `brew_temp_get_trim` | RTD via image dispatch | trimmed setpoint | ✅ | Unchanged logic. |
| SSR cutoff (safety) | `io_scan::apply_outputs` final gate | `img` inputs/sensors | `ssr_ctrl_set_duty` via `boiler_temp_apply_hw_duty` | ✅ | See §6. Final gate enforces standby/water/refill-error/descale/RTD-fault. Over-temp value gate added — see §6. |
| Relay cutoff (pump/solenoid/3-way/aux) | `io_scan::apply_outputs` | `img` outputs + `power_on` | `out_signals_set_level` | ✅ | Standby forces all four low every cycle. |
| Remote command precedence | §3 / §7 | — | — | ✅ | Deterministic, tested. |
| Over-temp cutout | `boiler_temp_process` (PID `is_over_threshold`) **and** `io_scan::apply_outputs` hard value gate | `img->temperatures[boiler].value` | `ssr_boiler_duty` → 0 | ✅ | Two independent layers, see §6. |
| Event-group status bits (`POWER_ON_BIT`, `BOILER_LEVEL_OK_BIT`, `DESCALE_MODE_BIT`) | `io_scan` syncs from image every cycle | `img` | `status_event_group` | 🔁 | Retained as a read-only projection of the image for display/HomeKit/boiler_temp consumers. |

**Orphan check:** every safety-critical output (SSR, pump, refill solenoid, 3-way, aux) has
exactly one authoritative writer path that terminates in `io_scan::apply_outputs`. No output
is driven from two competing sources.

---

## 2. Process image ownership model

Each field of `process_image_t` has exactly one **writer layer**. All other layers read only.
This is documented inline in [`process_image.h`](../../components/rebel-espresso/src/hw/base/process_image.h)
and summarised here.

| Field | Writer (owner) | Readers | Atomicity |
|-------|----------------|---------|-----------|
| `power_on` | I/O scan (edge) **and** `power_active/standby` (remote) | all | 32-bit atomic |
| `brew_on`, `steam_on`, `descale_mode` | I/O scan | control loop, comms | 32-bit atomic |
| `temperatures[]` | Sensor task | I/O scan (fault byte), control loop | `measure_t` may tear — consumers use fault byte for gating |
| `water_level_mv` | Sensor task | comms | 64-bit, tolerate 1-cycle-old |
| `water_level_ok` | Sensor task | I/O scan, control loop | 32-bit atomic |
| `ssr_boiler_duty` | Control loop | I/O scan | 32-bit atomic |
| `pump_on`, `refill_solenoid_on`, `three_way_on`, `aux_on` | I/O scan | I/O scan (apply) | 32-bit atomic |
| `refill_state`, `brew_active` | I/O scan | control loop, comms | 32-bit atomic |
| `brew_start_time_us`, `last_scan_time_us` | I/O scan | comms | 64-bit, tolerate 1-cycle-old |

**Note on `power_on` dual writer:** this is the one field with two writers by design (physical
edge + remote command). Both writers set the same semantic ("machine should be on"). The
"last transition wins" rule (§3) makes the outcome deterministic. No other field is shared.

**Torn-read policy:** critical safety decisions never depend on a multi-field snapshot. The
SSR fault gate reads only `temperatures[i].fault` (an 8-bit field, inherently atomic). The
over-temp value gate (§6) reads a single `double`; a torn read can only bias the machine
toward *cutting* the heater, never toward energising it, so it is safe by construction.

---

## 3. Standby / ON runtime paths (verified)

| Trigger | Path | Result |
|---------|------|--------|
| Physical switch ON | debounce edge → `img->power_on=true` → `POWER_ACTIVE` + refill restart + `aux_on=true` | outputs enabled next `apply_outputs` |
| Physical switch OFF | debounce edge → `img->power_on=false` → `POWER_STANDBY` + refill stop + solenoid/aux off | all outputs forced low ≤1 scan (20 ms) |
| Remote ON | `power_active()` sets `img->power_on=true` → io_scan sees `power_on != s_prev` → fires `POWER_ACTIVE` + refill restart | same as switch ON |
| Remote OFF | `power_standby()` sets `img->power_on=false` → io_scan fires `POWER_STANDBY` | same as switch OFF |
| Switch + remote conflict | whichever writer transitions **last** wins; io_scan only rewrites `power_on` on a *GPIO edge*, so remote state persists between edges | deterministic |

**Acceptance:** Standby forces SSR, pump, solenoid, 3-way and aux low within one scan cycle
(`apply_outputs` unconditional `!power_on` branch). ON restores normal control. ✅

---

## 4. Boiler refill verification

State machine (`boiler_refill_states.cpp`), driven every 20 ms by `io_scan::scan_refill`:

```
UNKNOWN ──(start_delay==0)──► IDLE (level ok) / ACTIVE (level low)
        └─(start_delay>0)───► STARTING ──(after delay)──► IDLE / ACTIVE
IDLE  ──(!level_ok for level_low_hysteresis_ms)──► ACTIVE
      ──(in_error)──► ERROR
ACTIVE──(level_ok for level_ok_hysteresis_ms)──► IDLE
      ──(in_error OR elapsed > max_refill_time_ms)──► ERROR
ERROR ── latched ── (only power_on cycle → UNKNOWN)
```

- **Hysteresis:** both directions gated (`level_low_hysteresis_ms`, `level_ok_hysteresis_ms`). ✅
- **Timeout:** ACTIVE longer than `max_refill_time_ms` → ERROR. ✅
- **Error latch:** `_state_error_process` is a no-op; only `boiler_refill_states_power_on()` clears it. ✅
- **Events:** `scan_refill` posts `BOILER_REFILL_STARTED/STOPPED/ERROR` on transitions for UI/telemetry. ✅
- **Scan rate:** driven at 20 ms (highest-priority task) — cannot be starved by comms/display. ✅
- **Standby safety:** `scan_refill` early-returns in standby/descale; even if it didn't, `apply_outputs` forces solenoid low when `!power_on`. ✅

---

## 5. RTD acquisition & freshness

- Sensor task calls `rtds_update(_rtd_cb)` every ~150 ms; callback writes each channel to
  `img->temperatures[idx]`. ✅
- Control loop (1 Hz) reads the latest values from the image and dispatches to
  `hw_specs_handle_new_temp` → `boiler_temp_process`. Freshness ≤150 ms, well inside the 1 s PID cadence. ✅
- **Boot-time safety:** `process_image_init` sets every channel `fault=1`, so the heater is
  inhibited until the sensor task produces a real reading. The control loop additionally skips
  faulted zero readings for the first 2 s (`now_us < 2000000`). ✅
- **Stale/torn reads:** gating uses the atomic `fault` byte; the temperature value is only
  used after the fault byte confirms validity. ✅

---

## 6. Safety cutoffs (final output gate)

All heater-inhibit conditions are enforced in `io_scan::apply_outputs`, the single final gate:

| Condition | Enforced at final gate | Also enforced upstream |
|-----------|------------------------|------------------------|
| Standby (`!power_on`) | SSR + all relays → 0 | control loop resets PID |
| Low water | SSR → 0 | `boiler_temp_process` |
| Refill error | SSR → 0 | — |
| Descale mode | SSR → 0 | `boiler_temp_process` |
| Boiler RTD fault | SSR → 0 (fault byte) | `boiler_temp_process` |
| **Over-temperature (value)** | **SSR → 0 when boiler temp > `IO_SCAN_OVERTEMP_LIMIT_C`** | `boiler_temp_process` PID `is_over_threshold` + >140 °C range check |

The over-temp **value** gate is defense-in-depth added during this audit: even if the control
loop wrote a stale non-zero duty, the final gate independently cuts the heater when the boiler
RTD reads above the hard limit. It only ever forces duty lower, never higher.

**Invariants:** outputs are written every cycle unconditionally; no control-path caller drives
the SSR or relays directly — `boiler_temp_apply_hw_duty` and `out_signals_set_level` are called
only from `apply_outputs`. `boiler_temp_set_duty` writes the *desired* duty to the image only. ✅

---

## 7. Remote command semantics (spec)

1. Remote ON sets `power_on=true`; the machine runs normally (subject to all safety gates).
2. Remote OFF sets `power_on=false`; standby forces every output low within one scan.
3. Remote ON **can** hold the machine on while the physical switch is OFF — `power_on` is only
   rewritten from the GPIO on a *debounced edge*, so it persists between edges.
4. **Last transition wins:** a physical switch flip (edge) after a remote command overrides the
   remote state, and vice-versa. There is no polling race because the physical switch only
   writes on an edge, and the remote only writes on an explicit command.

Conflict combinations covered by tests (§ test additions): remote ON/physical OFF, remote
OFF/physical ON, remote ON then physical OFF, physical ON then remote OFF.

---

## 8. PID control validation

- Cadence: 1 Hz hardware GP timer ISR → binary semaphore → `process_loop` task (priority 7). Unchanged. ✅
- Reads latest sensor data from the image (no direct SPI in the control loop → no contention jitter). ✅
- Output clipped to [0, 100] in `boiler_temp_process`; `ssr_ctrl_set_duty` re-clamps. ✅
- Over-temp dominates PID output (duty forced 0) both in the control loop and the final gate. ✅
- SPI/I2C load isolated: sensor task owns SPI; io_scan owns I2C relays; neither runs in the
  control-loop context. ✅

---

## 9. Transitional / hybrid leftovers

| Leftover | Location | Risk | Recommendation |
|----------|----------|------|----------------|
| `boiler_temp_process` reads `status_event_group` bits for power/water/descale instead of the process image | `boiler_temp.cpp` | Low — io_scan syncs the bits from the image every cycle, so state is consistent. Diverges from spec R4. | Migrate to read `process_image_get()` directly, then the event-group sync becomes display/HomeKit-only. Deferred: behaviour is currently correct; change is a conformance cleanup that needs its own test pass. |
| Event-group bits retained as a projection | `io_scan.cpp` step 3 | None | Keep as the read model for Layer 4 consumers (display, HomeKit) until they are migrated to the image. |
| `power.cpp` still owns `PIN_IN_SYS_EN` GPIO config | `power_init` | None | Move pin config into `io_scan_init` once legacy references are fully retired (noted in code TODO). |

No duplicate *control* source exists for any output — the leftovers above are read-model /
initialisation concerns, not competing writers.

---

## 10. End-to-end test coverage

See `test_app/main/test_io_scan.cpp`. Existing coverage: init safety, per-override safety logic,
debounce, remote override basics, brew edge, field isolation. Added in this pass: remote-precedence
conflict matrix, refill state-machine sequencing at scan rate, over-temp final gate, and
integration-style flow scenarios (boot standby/on, toggle sequences, low-water and descale inhibit).

---

## 11. Performance & timing (requires hardware)

The following acceptance criteria can only be measured on the physical target and are **not**
verifiable in QEMU or unit tests. Recommended methodology once hardware is available:

- **Input-to-output latency:** GPIO scope on `PIN_IN_BREW_EN` vs the pump relay line; expect
  ≤ one scan cycle + I2C write (~20–30 ms). Target from spec: < 40 ms.
- **Control-loop jitter:** log `esp_timer_get_time()` deltas between consecutive `TICK` posts under
  combined sensor + web load; expect < a few ms of jitter around 1000 ms.
- **Starvation check:** confirm io_scan (pri 8) and control loop (pri 7) both meet deadlines with
  the WDTs never firing under load.
- **SPI/I2C contention:** verify the sensor task never holds the SPI bus across a `vTaskDelay`
  (it does not — reads are bounded), so TFT redraws cannot stall RTD acquisition.

---

## Task completion status

| # | Task | Status | Evidence / notes |
|---|------|--------|------------------|
| 1 | Parity matrix | ✅ done | §1 of this doc — every critical behaviour mapped to an owner + status. |
| 2 | Explicit ownership model | ✅ done | §2 + `@owner` tags on every `process_image_t` field; dual-writer for `power_on` documented. |
| 3 | Standby/on behaviour | ✅ verified | §3 runtime-path table; covered by existing + new safety-gate tests. |
| 4 | Refill logic | ✅ verified | §4 + new refill sequencing tests (hysteresis, timeout, error latch, power-cycle recovery). |
| 5 | RTD acquisition & freshness | ✅ verified | §5; boot fault defaults + fault-byte gating confirmed. |
| 6 | Harden safety cutoffs | ✅ done | §6 + new over-temp value gate `IO_SCAN_OVERTEMP_LIMIT_C` in `io_scan::apply_outputs`; new tests. |
| 7 | Remote command precedence | ✅ done | §7 spec + new "last transition wins" conflict-matrix tests (4 combinations). |
| 8 | PID control validation | ✅ verified | §8; 1 Hz cadence, image-sourced temps, clamp + over-temp dominance confirmed. |
| 9 | Remove transitional leftovers | 🟡 partial | §9 — no duplicate *control* writers remain; `boiler_temp_process` still reads event-group bits (spec-R4 cleanup, deferred with its own test pass). |
| 10 | End-to-end integration tests | ✅ done | `test_io_scan.cpp` §10 flow scenarios (boot standby/on, low-water, descale) + refill sequencing. |
| 11 | Performance & timing | ⛔ hardware-only | §11 — methodology documented; not measurable in QEMU/CI. |
| 12 | Documentation cleanup | ✅ done | This doc + `realtime-io-architecture.md` status note + `AGENTS.md` rewritten to the 4-layer model. |

**Verification run (this pass):** `make format` clean · `make test` → **ALL TESTS PASSED**
(incl. new precedence/refill/over-temp/flow cases) · `make lint` on `io_scan.cpp` → passed
(warnings only in pre-existing headers). Full `make build` is currently blocked by pre-existing,
unrelated working-tree issues (untracked `esp32-aws-connector` needs `wifi_provisioning`;
`esp-homekit-sdk` submodule drift missing `esp_driver_gpio` in the `button` component REQUIRES) —
neither is touched by this work.

## Remaining work (not done in this pass)

1. **Task 9 cleanup** — migrate `boiler_temp_process` off `status_event_group` bits onto the process
   image directly (spec R4), then the event-group sync becomes display/HomeKit-only. Needs a
   dedicated test pass.
2. **GPIO ownership** — move `PIN_IN_SYS_EN` / `PIN_WATER_LEVEL_ENABLE` config into the scan/sensor
   tasks and delete the legacy config sites in `power.cpp` / `boiler_refill.cpp`.
3. **Task 11** — on-hardware timing capture (input→output latency, control-loop jitter, starvation).
4. **Build health (pre-existing, out of scope)** — resolve the `esp-homekit-sdk` submodule/IDF
   `button` REQUIRES incompatibility and the untracked AWS component's `wifi_provisioning` dependency
   so a full firmware build/lint can run in CI.

