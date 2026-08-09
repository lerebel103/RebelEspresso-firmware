# Water Probe Corrosion Monitoring & Predictive Maintenance Spec

> **Status:** proposed. Companion to the refill logic in
> [`boiler_refill_states.cpp`](../../components/rebel-espresso/src/machine/boiler_refill_states.cpp)
> and the probe acquisition in
> [`sensor_task.cpp`](../../components/rebel-espresso/src/runtime/sensor_task.cpp).

## Overview

The boiler water-level probe is a conductivity electrode: when submerged it
conducts and reads a **low** voltage; when exposed it reads a **high** voltage.
Refill is triggered whenever the reading is above `refill_mv_threshold`.

Over months of service the electrode **oxidises/corrodes**, which raises its
contact resistance. The failure is gradual but consistent: the **submerged
("wet") reading drifts upward** over time, toward the refill threshold. As the
margin between "wet" and "empty" collapses, the probe can no longer reliably
distinguish a full boiler from an empty one — producing intermittent false-low
readings that cause spurious refills and, ultimately, **boiler overfill**.

This feature adds:
1. **Predictive maintenance** — track the wet baseline over time, detect the
   corrosion trend, and surface a clear "service the probe" indication *before*
   it becomes dangerous.
2. **Protective interlock** — when the tracked reading reaches the corroded
   threshold *consistently*, latch a fault that **disables refill** (and keeps
   the heater safe), rather than continuing to refill on an untrustworthy probe.
3. **Diagnostics exposure** — report the probe voltage (a robust median) and the
   corrosion status/trend through the web UI, TFT, HomeKit, and MQTT
   (see the [MQTT / Home Assistant spec](mqtt-home-assistant-integration.md)).

## Background — current behaviour (what we build on)

- `sensor_task` reads the probe ADC (already averaged over `adc_num_readings`)
  and writes `water_level_mv` + a derived boolean
  `water_level_ok = (status == 0) && (voltage <= refill_mv_threshold)` into the
  process image. The probe is energised only momentarily and only when powered
  and not descaling (corrosion guard already in place).
- `boiler_refill_states` consumes the boolean: sustained not-OK → `ACTIVE`
  (refill on); sustained OK → `IDLE`; a single `ACTIVE` episode longer than
  `max_refill_time_ms` → latched `ERROR` (heater off).

Two gaps this spec closes:
- The single boolean collapses **"confirmed low"** and **"sensor fault/unknown"**
  into the same action (refill), which is fail-dangerous for overfill.
- `max_refill_time_ms` only caps a *single continuous* fill; a chattering probe
  produces many short fills that cumulatively overfill without ever tripping it.

## Requirements

### R1: Robust probe diagnostic value
- Maintain a rolling window of raw probe voltage samples (the `sensor_task`
  already produces one averaged reading per scan pass).
- Expose a **median** (or trimmed mean) over the window as the reported probe
  voltage — median rejects the intermittent spikes typical of a failing probe.
- Publish the diagnostic value into the process image so Layer 4 (web, TFT,
  HomeKit, MQTT) can read it without touching hardware.

### R2: Wet-reading tracking
- The corrosion signal is the **submerged** reading, so only sample it when the
  probe is *confidently wet*: immediately after a refill completes
  (`ACTIVE → IDLE`, level stable-OK for the full `level_ok_hysteresis_ms`).
- Maintain a slow **EMA / median** of these wet samples as the *monitored* wet
  reading, persisted in NVS so the trend survives reboots.
- Compare the monitored wet reading against the thresholds derived at calibration
  (R3). The `baseline_mv` reference is a **snapshot captured when the operator
  presses Calibrate** (R9), not a free-running value.
- Optionally track the **variance/noise** of the wet reading — rising noise is
  frequently an earlier corrosion indicator than the mean drift.

### R3: Corrosion thresholds (captured at calibration)
- Thresholds are derived from a **calibrated healthy baseline** plus a margin,
  since corrosion drives the wet reading *upward* from that baseline:
  - `warn_threshold_mv = baseline_mv + warn_margin_mv` → **SERVICE_SOON**
    (advisory, non-blocking).
  - `fault_threshold_mv = baseline_mv + fault_margin_mv` (larger margin) →
    **CORRODED_FAULT**.
- The **margins** are editable config knobs; the **baseline and derived
  thresholds** are captured/written when the operator presses Calibrate (R9) and
  are also directly editable for fine-tuning. All live in the config block (R8).
- Exact margins are unknown up front — ship reasonable **guessed defaults** and
  tune by editing the config directly until real-world behaviour is understood.

### R4: Corrosion status state machine
- Derived status: `OK` → `SERVICE_SOON` → `CORRODED_FAULT`.
- Require **consistency/debounce** before escalating (a single reading must not
  flip status): the monitored wet reading must exceed a threshold for a
  configurable sustained period / number of qualifying wet samples.
- Transitions are debounced in **both directions**: escalate when the reading
  stays above a threshold for `corrosion_consistency_ms`; **auto-clear**
  (de-escalate) when it stays back below the threshold, with hysteresis, for the
  same sustained period. No manual acknowledge is required.
- Status is a slow diagnostic — updated on the post-refill sampling cadence, not
  every 20 ms.

### R5: Protective interlock (the safety half)
Per the required behaviour: when the probe reaches the corroded threshold
consistently, **error out and do not refill**.
- While `CORRODED_FAULT` is active, the refill path is **inhibited** (solenoid +
  pump off via the existing safety gate) and the level is treated as untrusted so
  the heater is held safe. Unlike the `max_refill_time_ms` `REFILL_STATE_ERROR`
  (latched until power cycle), this interlock is **status-driven and auto-clears**
  when the corrosion status de-escalates (R4) — e.g. after the probe is cleaned or
  recalibrated.
- Additionally split the level signal into three cases so faults never drive a
  refill:
  - `LEVEL_OK` — confirmed submerged → no refill.
  - `LEVEL_LOW_CONFIRMED` — confirmed exposed (and probe trusted) → refill.
  - `LEVEL_UNKNOWN` — ADC `status != 0`, out-of-range voltage, or `CORRODED_FAULT`
    → **do not refill**, hold safe, raise error.
- The corrosion contribution to `LEVEL_UNKNOWN` is gated by `corrosion_guard_enabled`
  (R8). With the guard **off**, a `CORRODED_FAULT` no longer flips the level to
  untrusted, so the machine keeps its pre-corrosion behaviour (heater/refill
  unaffected) while voltage and status are still measured and reported — useful
  for field testing. A genuine ADC fault (`status != 0`) is **always** untrusted
  regardless of the guard.
- This is the acute backstop; corrosion trending (R2) is the predictive layer.
  They are complementary — a probe can also fail suddenly with no slow trend.

### R6: Reporting & operator indication
- **Web UI (System page)**: display the **current probe voltage** live, alongside
  the corrosion status, baseline, and margin-to-threshold, plus a **Calibrate**
  button (R9). The Status page shows corrosion status and a distinct
  "Service probe" banner for `SERVICE_SOON`/`CORRODED_FAULT`.
- **TFT**: reuse the existing fault-banner mechanism (as used for refill error)
  to show "Service Water Probe" / "Probe Fault".
- **HomeKit**: expose corrosion status via `StatusFault` (same pattern already
  used for the RTDs in `homekit.cpp`); the numeric voltage via a custom/Eve-style
  characteristic (HomeKit has no native voltage characteristic — see the note in
  Design Decisions).
- **MQTT / Home Assistant**: probe voltage + corrosion status as diagnostic
  entities, plus a **Calibrate** command (button) — specified in the
  [MQTT spec](mqtt-home-assistant-integration.md).

### R7: Persistence (NVS)
- Persist, in the probe/refill NVS namespace: the wet-baseline EMA, the
  calibrated healthy reference, min/max seen, and the current corrosion status +
  last-update timestamp.
- Keep the NVS footprint tiny and **rate-limit writes** (baseline changes slowly;
  write at most on status change or on a slow cadence) to avoid flash wear.

### R8: Configuration block (follows the existing pattern)
- Corrosion parameters belong to the water-probe/refill domain. Extend the
  existing `boiler_refill` config block (shared probe) with:
  - `corrosion_enabled` (bool)
  - `corrosion_guard_enabled` (bool) — when set, a `CORRODED_FAULT` gates the
    heater and refill (default on). Clear it to restore pre-corrosion machine
    behaviour while still monitoring/reporting the probe voltage and status.
  - `corrosion_baseline_mv` (uint16) — captured at calibration
  - `corrosion_warn_margin_mv` (uint16) — added to baseline → warn threshold
  - `corrosion_fault_margin_mv` (uint16) — added to baseline → fault threshold
  - `corrosion_warn_threshold_mv` / `corrosion_fault_threshold_mv` (uint16) —
    derived on calibrate, directly editable for fine-tuning
  - `corrosion_ema_alpha` (fixed-point / permille)
  - `corrosion_consistency_ms` (sustained period for escalation)
- Implement via `from_json`/`to_json` with stable NVS keys and `_DEFAULT`
  constants, exactly like the existing refill fields; surface them in the
  `boiler_refill` section of the web Config page. The Calibrate **action** itself
  is a System-page endpoint (R9), not a config field. Existing NVS keys are
  unchanged (backward compatible).

### R9: Calibration (operator-triggered)
- Primary mechanism is a **Calibrate button** on the web **System** page and an
  equivalent **Home Assistant command** (MQTT button). Pressed when the probe is
  known-good (clean electrode, boiler full/submerged).
- On press, the firmware:
  1. captures the current median probe voltage as `baseline_mv`,
  2. computes `warn_threshold_mv = baseline + warn_margin_mv` and
     `fault_threshold_mv = baseline + fault_margin_mv`,
  3. persists baseline + both thresholds to NVS,
  4. clears any existing corrosion fault (also acts as the acknowledge/reset).
- Exposed as a **System API action** (like reboot/factory-reset), not a config
  field. The margins used are the current config values (guessed defaults,
  tunable).
- Captures the actual healthy reading on this machine, so it absorbs
  water-hardness/installation differences without a hardcoded reference.

### R10: Hardware scope & testing
- Hardware revision 2 only.
- All decision logic (baseline EMA update, threshold escalation, three-way level
  classification, protective transition) must be **pure and unit-tested** in the
  `test_app` suite, with no hardware or FreeRTOS dependencies — mirroring the
  existing `io_scan_modes` / `boiler_refill_states` test approach.

## Design Decisions

- **Track the wet baseline, not raw voltage.** Averaging the raw signal mostly
  measures the fill/empty duty cycle. Corrosion specifically drifts the
  *submerged* reading, so we sample only when confidently wet (post-refill).
- **Median for the reported value.** Failing probes are spiky; the median (or a
  trimmed mean) is far more stable for display/telemetry than a plain mean.
- **Thresholds as margins above the calibrated baseline.** Corrosion raises the
  wet reading, so both thresholds sit *above* the healthy baseline captured at
  Calibrate time. Margins ship as **guessed defaults** and are tunable by editing
  the config directly until the real-world behaviour is characterised.
- **Confounders.** Water hardness, descaling residue, and temperature all shift
  absolute conductivity. Mitigated by (a) trending *drift from a calibrated
  reference*, (b) self-calibration, and (c) sampling at a consistent point in the
  cycle. We track **relative change**, not absolute mV.
- **Predictive ≠ protective.** The EMA trend gives early "service soon"; the
  three-way level classification + corroded-fault interlock give the acute
  overfill protection. Ship both.
- **Fault policy: auto-clear.** Both `SERVICE_SOON` and `CORRODED_FAULT`
  auto-clear when the monitored wet reading returns below the corresponding
  threshold for a sustained period (symmetric to the escalation consistency in
  R4), e.g. after the probe is cleaned or recalibrated. No manual acknowledge is
  required; hysteresis prevents status chatter.
- **HomeKit limitation.** No standard voltage characteristic; status maps to
  `StatusFault`, value needs a custom characteristic. HA/MQTT is the richer path
  for numeric diagnostics — hence the companion MQTT spec.

## Open Questions
1. Starting **guessed defaults** for `warn_margin_mv` / `fault_margin_mv` (mV to
   add above the calibrated baseline) — pick initial values to ship and tune.
2. Persist only the EMA/baseline + min/max, or also a short rolling history
   (weekly points) for a trend graph — accepting slightly more flash wear?
3. Extend the `boiler_refill` config section (recommended) or introduce a
   dedicated `water_probe` config section?

## Suggested Milestones
- **M1** — Diagnostic value + process-image plumbing (median, `water_level_mv`
  already present); display the **current probe voltage** on the web System page.
  No behaviour change.
- **M2** — **Calibrate** action (System page + API): capture baseline, derive and
  persist thresholds to NVS; corrosion status (advisory only) with web/TFT/HomeKit
  indication.
- **M3** — Three-way level classification + corroded-fault protective interlock
  (no refill on fault/unknown); cumulative-overfill hardening if desired.
- **M4** — MQTT diagnostics exposure + Calibrate command (in the MQTT spec).
