# Manual Hardware Validation Checklist

This document defines the required on-device validation for firmware changes that affect
real-time I/O, safety interlocks, machine state, or control behavior.

## Purpose

Some firmware behavior cannot be fully validated by unit tests or CI alone, especially:
- input-to-output latency
- relay/SSR timing
- physical switch debounce
- remote-vs-physical precedence
- boiler refill behavior under real sensor conditions
- watchdog and timing behavior under load
- recovery from power cycles and fault states

Any change that touches these paths should be validated on a physical test board before merge,
or explicitly documented as requiring deferred hardware validation if a board is unavailable.

## Required validation areas

### 1. Boot and safe defaults
Verify:
- board boots cleanly
- no reset loop
- all outputs start OFF
- heater SSR remains OFF at boot
- refill outputs remain OFF at boot
- no unsafe state appears before sensors initialize

Pass criteria:
- board reaches idle state safely
- no unexpected output activation

### 2. Standby / active transitions
Verify:
- physical power ON transitions machine to active state
- physical power OFF transitions machine to standby
- standby forces heater and relays OFF
- outputs recover correctly on re-enable

Pass criteria:
- standby is safe
- active mode resumes correctly

### 3. Remote command precedence
Verify the intended precedence rule:
- remote ON while physical OFF
- remote OFF while physical ON
- remote ON then physical OFF
- physical ON then remote OFF

Pass criteria:
- last transition wins
- machine state is deterministic
- no oscillation or ambiguity occurs

### 4. Boiler refill logic
Verify:
- refill enters ACTIVE when level is low
- refill returns to IDLE when level recovers
- timeout/error paths latch as intended
- power cycle clears refill error only if specified

Pass criteria:
- refill behaves consistently with the documented state machine
- heater remains inhibited when refill safety requires it

### 5. RTD / sensor behavior
Verify:
- normal temperature readings appear plausible
- sensor fault is detected
- sensor fault inhibits heater output
- sensor recovery behaves as expected

Pass criteria:
- readings are stable
- faults force safe behavior

### 6. Heater safety cutoffs
Verify each condition independently:
- standby
- low water
- descale mode
- RTD fault
- refill error
- over-temperature cutoff

Pass criteria:
- heater duty drops to zero for each unsafe condition
- heater does not re-enable until the condition clears

### 7. PID behavior
Verify:
- heater output changes plausibly as temperature approaches setpoint
- no runaway output
- no stuck-on heater command
- no obvious timing instability

Pass criteria:
- duty tracks expected behavior
- control remains stable

### 8. Timing and latency
Verify:
- input changes are reflected in outputs within the intended scan period
- no visible lag beyond the expected real-time loop
- no watchdog resets during normal operation
- no starvation under mixed sensor / UI / network load

Pass criteria:
- timing matches design expectations
- system remains responsive and stable

### 9. Power-cycle recovery
Verify:
- safe state returns after power-off / power-on cycle
- fault latches recover only as intended
- no stale unsafe state survives reboot

Pass criteria:
- boot and recovery are safe and deterministic

## Test recording template

For each run, record:
- date
- firmware commit / PR
- board / hardware revision
- tester name
- test cases executed
- observed behavior
- pass/fail
- notes / anomalies

## Merge gate guidance

For changes that affect:
- safety interlocks
- heater control
- power state
- refill control
- sensor acquisition
- output timing

this hardware validation is considered a required step unless the change is purely documentation or has been explicitly exempted.