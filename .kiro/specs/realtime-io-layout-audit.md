# Real-Time I/O Architecture — Repository Layout Audit & Target Proposal

Companion to [`realtime-io-architecture.md`](realtime-io-architecture.md) and
[`realtime-io-parity-audit.md`](realtime-io-parity-audit.md).

This document audits how the current file layout maps to the runtime architecture, proposes a
target layout, and records which structural changes were **applied now** versus **deferred**.

## Update — single-board R2 simplification pass (applied)

Since the original audit below was written, the following was **applied and verified**:

- **`src/hw/base/` → `src/control/`** (git mv). The misleading "hardware base/HAL" name is gone;
  shared runtime/control logic now lives under `control/`. All path-rooted includes
  (`<src/hw/base/…>` in `display.cpp`, `hw_specs.cpp`, `thing_info.cpp`), the component
  `CMakeLists.txt` (SRC_DIRS + INCLUDE_DIRS), and the test-app references were updated.
- **Deleted `components/rebel-espresso/test/`** — an orphaned pre-refactor test harness
  (unreferenced by any build; its tests `#include <src/control/…>`, a path that did not exist).
- **Deleted `src/hw/r2/src/sdkconfig`** — an unreferenced stale sdkconfig duplicate.

**Verification:** `make test` → ALL TESTS PASSED; firmware `idf.py reconfigure` (with the unrelated
untracked AWS components temporarily set aside) → **CONFIGURE OK**; per-TU `-fsyntax-only` compile
of every file whose includes changed (`display.cpp`, `hw_specs.cpp`, `thing_info.cpp`) plus
representative `control/` files (`io_scan`, `boiler_temp`, `sensor_task`) → **all OK**. (The one
remaining pre-existing failure, `process_loop.cpp`/`controller.cpp` → `hal/timer_types.h`, is an
ESP-IDF-6.0 header removal unrelated to this rename.)

**Deferred: `src/hw/r2/src/` → `src/board/`.** The R2 folder is build-config-entangled: the
committed `sdkconfig` sets the partition table to `.../src/hw/r2/partitions.csv` while
`sdkconfig.defaults` points at the repo-root `partitions.csv`. Renaming `hw/r2` touches a
build-critical, **unverifiable** partition path (partition-table generation needs a full firmware
build, which is blocked here). This move is deferred to a CI-backed change and should first
reconcile the two partition-table paths.

> **Constraint that shaped this pass.** The full firmware build (`make build`) cannot currently
> be completed in this workspace due to pre-existing, unrelated issues (untracked
> `esp32-aws-connector` needs `wifi_provisioning`; `esp-homekit-sdk` submodule drift misses
> `esp_driver_gpio` in the `button` component). Only the QEMU unit-test app (`make test`) plus
> firmware-configure + per-TU compile checks are verifiable here.

---

## 1) Current layout vs runtime responsibility

Everything hardware-independent lives flat in `components/rebel-espresso/src/hw/base/`. That single
folder now mixes all four runtime layers, the shared state, control modules, communication/UI, and
transitional glue:

| File | Runtime responsibility | Layer |
|------|------------------------|-------|
| `process_image.{h,cpp}` | Shared state bridging all layers (+ seqlock accessors) | **State (cross-cutting)** |
| `io_scan.{h,cpp}` | 20 ms scan: debounce, refill drive, output write | Layer 1 |
| `io_scan_safety.{h,cpp}` | Pure final safety gate | Layer 1 (safety) |
| `sensor_task.{h,cpp}` | RTD + water-level acquisition | Layer 2 |
| `process_loop.{h,cpp}` | 1 Hz PID control loop | Layer 3 |
| `boiler_temp.{h,cpp}` | PID heater control + interlocks | Layer 3 |
| `brew_temp.{h,cpp}` | Brew-head temp trim | Layer 3 |
| `boiler_refill_states.{h,cpp}` | Refill state machine (pure) | Layer 3 logic |
| `boiler_refill.{h,cpp}` | Refill **config/NVS + status** (state machine moved out) | Config/persistence |
| `power.{h,cpp}` | Remote standby/active API + power-switch GPIO config | Layer 1 API (transitional) |
| `brew.{h,cpp}` | Brew shot statistics (event-driven) | Layer 4 |
| `iot.{h,cpp}` | Communication loop (web, OTA, HomeKit) | Layer 4 |
| `schedules.{h,cpp}` | Wake schedules | Layer 4 |
| `display.h` / TFT (r2) | Display | Layer 4 |
| `setpoint_selector.{h,cpp}` | UI setpoint selection | Layer 4 |
| `shadow_helper.{h,cpp}` | Cloud shadow glue | Layer 4 |
| `schedules`, `reset_button`, `device_info`, `eeprom`, `app_metrics` | Infra / device services | Support |
| `hw_specs.h`, `out_signals.h`, `rtds.h`, `display.h` | HW abstraction interfaces (impl in `hw/r2/src`) | HAL |
| `controller.{h,cpp}` | Init orchestration + enters loop | Bootstrap |

### Mismatches identified

1. **`hw/base` is a misleading catch-all.** The name implies "hardware base/HAL", but it holds the
   entire control stack, comms, UI, and infra. A newcomer cannot infer the layered runtime model
   from the folder.
2. **The four runtime layers are not visually grouped.** `io_scan`, `sensor_task`, `process_loop`
   sit next to `schedules`, `shadow_helper`, `eeprom` with no separation by responsibility or
   safety criticality.
3. **The process image — the architectural centre — is not first-class.** It is one file among ~40
   in a flat folder, despite being the single source of truth every layer depends on.
4. **Safety-critical code is interleaved with Layer-4 glue.** `io_scan_safety.cpp` (the final SSR
   gate) sits beside `setpoint_selector.cpp` and `schedules.cpp`.
5. **Transitional modules are not marked.** `power.cpp` (now a thin remote API + a GPIO-config site
   that should move to `io_scan`), the `status_event_group` projection inside `io_scan.cpp`, and the
   split of refill into `boiler_refill.cpp` (config) vs `boiler_refill_states.cpp` (logic) are not
   obviously transitional.
6. **Tests do not map 1:1 to runtime areas.** `test_io_scan.cpp` had grown to cover process-image
   init, seqlock, safety gate, debounce, precedence, refill, flow, and boiler cutoffs.

---

## 2) Proposed target layout

Minimal, responsibility-grouped, and safety-forward. Grouping is by **runtime layer**, with the
process image and safety gate promoted to first-class locations:

```
components/rebel-espresso/src/
├── core/                     ← cross-cutting architectural state
│   └── process_image.{h,cpp} ← shared state + seqlock accessors (first-class)
├── rt/                       ← safety-critical real-time layers (1–3)
│   ├── io_scan.{h,cpp}       ← Layer 1: scan + output write
│   ├── io_scan_safety.{h,cpp}← Layer 1: pure final safety gate
│   ├── sensor_task.{h,cpp}   ← Layer 2: acquisition
│   ├── process_loop.{h,cpp}  ← Layer 3: 1 Hz PID loop
│   ├── boiler_temp.{h,cpp}   ← Layer 3: heater PID + interlocks
│   ├── brew_temp.{h,cpp}     ← Layer 3: brew trim
│   └── boiler_refill_states.{h,cpp} ← Layer 3: refill state machine (pure)
├── app/                      ← Layer 4: comms / UI / services (non-real-time)
│   ├── iot.{h,cpp}  schedules.{h,cpp}  setpoint_selector.{h,cpp}
│   ├── brew.{h,cpp} (stats)  shadow_helper.{h,cpp}  reset_button.{h,cpp}
│   └── ...
├── config/                   ← persisted configuration owners
│   └── boiler_refill.{h,cpp} (refill cfg/NVS/status)
├── compat/                   ← transitional glue, obvious and removable
│   └── power.{h,cpp} (remote API; GPIO-config to fold into io_scan)
├── hal/                      ← hardware abstraction interfaces
│   └── hw_specs.h out_signals.h rtds.h display.h
├── sys/  utils/  homekit/    ← unchanged
```

Acceptance mapping: each folder has one clear purpose (`core` = state, `rt` = safety-critical
real-time, `app` = non-critical, `config` = persistence, `compat` = transitional, `hal` = HW). The
runtime model is legible at a glance, and safety-critical code is not mixed with Layer-4 glue.

### Why not applied now
Moving these requires editing `COMPONENT_SRCDIRS`/`INCLUDE_DIRS` in
`components/rebel-espresso/CMakeLists.txt` and every `#include "..."` that uses a bare filename.
Because the firmware build is not runnable here, the move cannot be verified end-to-end, so it is
deferred to a CI-backed change (see task 9 / non-goals: no unverifiable structural moves).

---

## 3) Transitional / compatibility code (flag for later removal)

| Item | Location | Why transitional | Removal trigger |
|------|----------|------------------|-----------------|
| `power.cpp` GPIO config | `power_init()` | `PIN_IN_SYS_EN` config belongs to the I/O scan owner | Fold into `io_scan_init`, delete `power_init` |
| `status_event_group` projection | `io_scan.cpp` step 3 | Read model kept only for display/HomeKit | Migrate those consumers to the process image |
| Refill config vs logic split | `boiler_refill.cpp` + `boiler_refill_states.cpp` | Config module is the residue of the old monolith | Keep config; it is the steady-state persistence owner |

Recommendation: once CI can build firmware, place `power.{h,cpp}` under `compat/` so its transitional
status is visible, and add a `// TRANSITIONAL:` header banner to each item above.

---

## 4) Process image as a first-class component
- **Proposed:** relocate `process_image.{h,cpp}` to `core/` and keep the seqlock accessors
  (`process_image_read_temp/write_temp`) beside it (they already are).
- **Applied now (no move needed):** the ownership model is documented inline (`@owner` tags, the
  `power_on` dual-writer exception, and the seqlock concurrency contract), and the process image
  now has dedicated unit tests (see §7). This makes its central role obvious without a move.

## 5) Runtime module boundaries
Each runtime module already has a single primary responsibility (see §1 table). The main boundary
smell is **folder co-location**, not code coupling: the modules do not own each other's state —
they communicate only through the process image and events. The target layout (§2) reinforces the
existing clean boundaries; no code-level decoupling is required.

## 6) Documentation layout
Predictable locations, now cross-linked:
- Architectural truth → [`.kiro/specs/realtime-io-architecture.md`](realtime-io-architecture.md)
- Outstanding gaps / parity → [`.kiro/specs/realtime-io-parity-audit.md`](realtime-io-parity-audit.md)
- This layout audit → `realtime-io-layout-audit.md`
- Required manual validation → [`docs/testing/manual-hardware-validation.md`](../../docs/testing/manual-hardware-validation.md)
- Merge-gate + validation policy → `AGENTS.md` (Pre-Commit Gate, Required Hardware Validation)

A **Documentation map** was added to `AGENTS.md` so these are discoverable from the rules file.

## 7) Tests reflecting the architecture (applied)
`test_process_image.cpp` was split out of `test_io_scan.cpp` so tests map to runtime areas:

| Test file | Area verified |
|-----------|---------------|
| `test_process_image.cpp` | Process image init safety, field isolation, seqlock snapshots |
| `test_io_scan.cpp` | I/O scan safety gate, debounce, remote precedence, refill sequencing, flow |
| `test_safety_interlocks.cpp` | `boiler_temp` interlocks, SSR clamping, refill safety |
| `test_pid.cpp` | Control algorithm (hardware-agnostic) |

---

## Applied vs deferred (summary)

**Applied now (verified with `make test`):**
- Split `test_process_image.cpp` from `test_io_scan.cpp` (§7).
- Added a Documentation map to `AGENTS.md` (§6).
- This audit/proposal document (§1–§5).

**Deferred to a CI-backed change (needs a full firmware build to verify):**
- Physical folder reorg into `core/ rt/ app/ config/ compat/ hal/` (§2).
- Relocating `process_image` to `core/` and `power` to `compat/` (§3–§4).
- Folding `power.cpp`'s GPIO config into `io_scan_init` and removing the event-group projection.
