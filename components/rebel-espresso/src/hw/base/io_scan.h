#pragma once

/**
 * I/O Scan Task — Layer 1 of the control architecture.
 *
 * Runs at 20ms (50Hz), highest FreeRTOS priority (8).
 * Responsibilities:
 *   - Read all switch GPIO inputs with software debounce
 *   - Apply safety overrides to desired outputs
 *   - Write all physical outputs unconditionally every cycle
 *   - Drive boiler refill state machine (future)
 *   - Track brew state transitions
 *   - Enrolled in task WDT (200ms timeout)
 *
 * The I/O scan is the last gate before hardware. Safety overrides here
 * cannot be bypassed by any other layer.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise and start the I/O scan task.
 * Must be called after GPIO and I2C peripherals are initialised.
 */
void io_scan_init(void);

/**
 * Stop the I/O scan task and release resources.
 */
void io_scan_delete(void);

#ifdef __cplusplus
}
#endif
