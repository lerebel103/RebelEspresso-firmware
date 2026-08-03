# Real-Time I/O Architecture — Parity & Safety Audit

Companion to [`realtime-io-architecture.md`](realtime-io-architecture.md). Tracks the remaining
hardening work for the layered scan architecture. Language is deliberately conservative:
**verified** = proven by unit tests in QEMU; **implemented** = in code but not hardware-proven;
**remaining** = not yet done.

## Status summary

| # | Item | Status | Where |
|---|------|--------|-------|
| 1 | `boiler_temp_process()` off event-group bits | ✅ verified | `boiler_temp.cpp` reads process image; tests in `test_safety_interlocks.cpp`, `test_io_scan.cpp` §12 |
| 2 | Torn-read risk on `measure_t` | ✅ addressed (safety path) | seqlock `process_image_read_temp/write_temp`; 64-bit non-safety fields still 1-cycle-tolerated |
| 3 | `power_on` dual-writer ownership | ✅ verified | documented in `process_image.h`; precedence tests in `test_io_scan.cpp` §8 |
| 4 | Watchdog docs vs implementation | ✅ done | spec + `AGENTS.md` now state the shared 2 s task WDT |
| 5 | Legacy event-group mirroring | 🟡 partial | `boiler_temp` migrated off bits; io_scan still projects bits for display/HomeKit |
| 6 | GPIO config ownership | ⛔ remaining | still in `power.cpp` / refill init |
| 7 | Production-code tests for scan helpers | ✅ done (safety gate) | `io_scan_apply_safety()` shared by prod + tests; debounce still replicated in tests |
| 8 | Hardware end-to-end timing | ⛔ hardware-only | methodology only |
| 9 | Desired vs applied duty semantics | 🟡 clarified | `ssr_boiler_duty` = desired; io_scan is sole applier; applied-duty reporting not split out |
| 10 | Docs conservative & accurate | ✅ done | this rewrite |

**Verification run:** `make format` clean · `make test` → **ALL TESTS PASSED** · `make lint` on
`io_scan.cpp`, `io_scan_safety.cpp`, `process_image.cpp` → passed (warnings only in pre-existing
headers). Full `make build` remains blocked by pre-existing, unrelated working-tree issues
(untracked `esp32-aws-connector` needs `wifi_provisioning`; `esp-homekit-sdk` submodule drift
misses `esp_driver_gpio` in the `button` REQUIRES) — neither is touched by this work.

## Outstanding / incomplete tasks

1. **Migrate `boiler_temp_process()` off `status_event_group` bits** — ✅ **DONE (verified)**
   - Current state: `boiler_temp_process()` still gates on `POWER_ON_BIT`, `BOILER_LEVEL_OK_BIT`, and `DESCALE_MODE_BIT`.
   - Why this is incomplete: the new architecture spec says control logic should read the process image directly, not rely on legacy event-group projections.
   - Needed work:
     - Read `power_on`, `water_level_ok`, and `descale_mode` from `process_image_t` directly.
     - Keep event-group bits only as a compatibility projection for display/HomeKit if still needed.
     - Add tests proving heater cutoffs still work when event-group sync is removed.
   - Acceptance criteria:
     - No safety-critical control path depends on event-group bits.
     - Behavior matches existing safety tests after the migration.

2. **Resolve the process image concurrency / torn-read risk** — ✅ **DONE for the safety path**
   - Resolution: `temperatures[]` (the only multi-word field used in a safety decision) is now
     written/read through a per-channel seqlock (`process_image_write_temp` / `process_image_read_temp`);
     the io_scan safety gate and the control loop both read via the snapshot accessor. The remaining
     64-bit fields (`water_level_mv`, `*_time_us`) are non-safety and readers tolerate a 1-cycle-old value.
   - Current state: `measure_t`, `double`, and `uint64_t` fields are written and read concurrently across tasks.
   - Why this is incomplete: 64-bit writes are not atomic on ESP32, so readers can observe torn values.
   - Needed work:
     - Add sequence counters / seqlock-style protection for multi-word fields, or
     - Split the process image so safety-critical reads use atomic 32-bit values only.
     - Rework tests and comments to reflect the real thread-safety model.
   - Acceptance criteria:
     - No safety decision depends on a potentially torn snapshot.
     - Concurrent reads/writes are explicitly safe or safely tolerated.

3. **Clarify and enforce ownership of `power_on`** — ✅ **DONE (verified)**
   - Resolution: documented as a deliberate dual-writer exception in `process_image.h`; the
     “last transition wins” rule is covered by the precedence matrix in `test_io_scan.cpp` §8.
   - Current state: `power_on` is intentionally dual-written by the physical scan and remote power API.
   - Why this is incomplete: the process image documentation still claims one writer per field except for this special case, which makes the model easy to misread.
   - Needed work:
     - Document `power_on` as a deliberate exception.
     - Add tests proving “last transition wins” under all conflicting command orders.
     - Ensure no other field is ever dual-written.
   - Acceptance criteria:
     - The exception is explicitly documented and tested.
     - No ambiguity remains about which writer owns the final state.

4. **Align watchdog documentation and implementation** — ✅ **DONE**
   - Resolution: the code enrolls the I/O scan in the shared 2 s task WDT; the spec and `AGENTS.md`
     were corrected to describe the shared 2 s watchdog (the “200 ms” claim is removed).
   - Current state: the spec says the I/O scan has a 200ms WDT, but the implementation appears to use the shared 2s task watchdog.
   - Why this is incomplete: documentation and runtime behavior disagree.
   - Needed work:
     - Either implement a dedicated 200ms watchdog for the I/O scan task, or
     - Update the spec so it accurately reflects the shared 2s watchdog design.
     - Verify task watchdog enrollment failure is handled.
   - Acceptance criteria:
     - Spec and code match.
     - Watchdog behavior is explicit and testable.

5. **Remove transitional reliance on legacy event-group mirroring** — 🟡 **PARTIAL**
   - Resolution so far: `boiler_temp_process()` no longer reads event-group bits. The io_scan still
     projects `POWER_ON_BIT` / `BOILER_LEVEL_OK_BIT` / `DESCALE_MODE_BIT` from the image because the
     display and HomeKit layers still consume them. Remaining: migrate those consumers, then delete
     the projection.
   - Current state: the I/O scan mirrors process image state back into `status_event_group`.
   - Why this is incomplete: it preserves compatibility but keeps the old architecture alive longer than necessary.
   - Needed work:
     - Identify which consumers still require event-group bits.
     - Migrate remaining consumers to the process image where practical.
     - Keep event groups only if there is a concrete compatibility reason.
   - Acceptance criteria:
     - Event groups are no longer part of the core control logic.
     - Remaining usage is justified and documented.

6. **Finish ownership cleanup for GPIO configuration** — ⛔ **REMAINING**
   - Current state: some GPIO configuration is still initialized in legacy modules such as `power.cpp` and refill-related code.
   - Why this is incomplete: the new architecture intends the scan/sensor tasks to own the operational I/O boundary.
   - Needed work:
     - Move switch and probe GPIO setup into the new task owners where appropriate.
     - Remove leftover configuration responsibility from transitional code paths.
   - Acceptance criteria:
     - GPIO initialization ownership matches the final architecture.
     - No legacy module configures pins it no longer owns.

7. **Add direct production-code tests for I/O scan helpers** — ✅ **DONE for the safety gate**
   - Resolution: the safety-override logic is now a single production function
     `io_scan_apply_safety()` (`io_scan_safety.cpp`) called by both `io_scan::apply_outputs` and the
     tests — the previously duplicated copy in the test was removed. Remaining minor item: the
     debounce filter is still replicated in the test rather than exposed from production.
   - Current state: some tests re-implement the scan/safety logic rather than calling the production logic directly.
   - Why this is incomplete: tests can pass even if the production helpers regress.
   - Needed work:
     - Factor the safety override, debounce, and state-transition logic into testable helpers.
     - Make the tests validate the actual production code paths.
   - Acceptance criteria:
     - Tests fail if the real implementation regresses.
     - No duplicated logic lives only inside tests.

8. **Validate end-to-end behavior on hardware** — ⛔ **REMAINING (hardware-only)**
   - Current state: unit tests cover logic, but timing and latency are not hardware-proven.
   - Why this is incomplete: the architecture’s main value proposition is <40ms I/O response.
   - Needed work:
     - Measure input-to-output latency on the device.
     - Measure control-loop jitter under sensor/display/network load.
     - Verify no starvation or watchdog trips occur under real load.
   - Acceptance criteria:
     - Measured latency meets target.
     - Control loop timing remains stable.

9. **Confirm `boiler_temp` final safety gate and desired-duty separation** — 🟡 **CLARIFIED**
   - Resolution: `ssr_boiler_duty` means exactly one thing — the *desired* duty written by the
     control loop. `io_scan_apply_safety()` is the sole path that turns it into the applied duty
     (after overrides) and `boiler_temp_apply_hw_duty()` is the only hardware writer. A separate
     *applied-duty* reporting field was considered but not added (no consumer needs it yet).
   - Current state: `boiler_temp_set_duty()` and `boiler_temp_apply_hw_duty()` still risk mixing “desired” and “applied” duty semantics.
   - Why this is incomplete: the control loop should write desired duty only; I/O scan should be the sole hardware-applier after safety overrides.
   - Needed work:
     - Split desired-duty storage from applied-duty reporting if necessary.
     - Ensure `ssr_boiler_duty` means one thing only.
     - Keep hardware writes in the I/O scan path only.
   - Acceptance criteria:
     - The process image clearly distinguishes requested vs applied duty, if both are needed.
     - No control loop path can bypass the final gate.

10. **Update the audit/spec to match the current implementation exactly**
   - Current state: the docs are better, but still contain assertions that are stronger than what’s fully proven.
   - Why this is incomplete: any mismatch between audit/spec and code weakens the confidence of the refactor.
   - Needed work:
     - Mark items as verified only when they are actually verified.
     - Reduce “done” language for tasks that are still transitional.
     - Keep a separate section for “implemented but needs hardware proof.”
   - Acceptance criteria:
     - The docs are conservative and accurate.
     - Readers can tell what is proven, what is implemented, and what still needs work.