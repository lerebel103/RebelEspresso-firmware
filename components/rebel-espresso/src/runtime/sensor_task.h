#pragma once

/**
 * ADC / Sensor Task — Layer 2 of the control architecture.
 *
 * Owns all analog sensor reads (RTD temperatures via ADS124S08, water level ADC).
 * Runs free at ~5-10Hz (100-200ms sleep between passes), priority 6.
 * Writes results to the process image for consumption by the control loop
 * and I/O scan task.
 *
 * Shares the SPI bus with the TFT display via ESP-IDF device-level mutex.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise and start the sensor task.
 * Must be called after SPI bus and RTDs are initialised (rtds_init).
 */
void sensor_task_init(void);

/**
 * Stop the sensor task.
 */
void sensor_task_delete(void);

#ifdef __cplusplus
}
#endif
