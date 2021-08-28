#pragma once

#include <driver/adc.h>

// SPI bus
#define MAX_SPI_WAIT_TICKS     (5000 / portTICK_PERIOD_MS)
#define PIN_MISO                GPIO_NUM_12
#define PIN_MOSI                GPIO_NUM_13
#define PIN_SCK                 GPIO_NUM_14
// ADC (ADS124S08)
#define PIN_OUT_ADC_CS          GPIO_NUM_15
#define PIN_OUT_ADC_RESET       GPIO_NUM_16
// Reference resistor used to create VRef
#define ADC_RREF                2000.0f

// I2C bus and attached peripherals
#define I2C_MASTER_NUM          0 /*!< I2C master i2c port number*/
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_PIN_SDA             GPIO_NUM_19
#define I2C_PIN_SCL             GPIO_NUM_18
#define I2C_IO_EXPANDER_ADDRESS 0x41u
#define I2C_EEPROM_ADDRESS      0x50u

// Switches (inputs)
#define PIN_IN_SYS_EN           GPIO_NUM_35
#define PIN_IN_BREW_EN          GPIO_NUM_34
#define PIN_IN_STEAM_EN         GPIO_NUM_17
#define PIN_IN_AUX_EN           GPIO_NUM_39

// Buttons (inputs)
#define PIN_IN_ENTER            GPIO_NUM_4
#define PIN_IN_BACK             GPIO_NUM_23
#define PIN_IN_UP               GPIO_NUM_36
#define PIN_IN_DOWN             GPIO_NUM_5

// SSR Outputs
#define PIN_OUT_SSR1            GPIO_NUM_27
#define PIN_OUT_SSR2            GPIO_NUM_21

// Water sensing
#define PIN_WATER_LEVEL_ENABLE  GPIO_NUM_32

// TFT display
#define PIN_OUT_DISPLAY_CS      GPIO_NUM_33
#define PIN_OUT_DISPLAY_DC      GPIO_NUM_22
#define PIN_OUT_DISPLAY_LED     GPIO_NUM_25
#define PIN_OUT_DISPLAY_RESET   GPIO_NUM_26

// This doesn't really belong here
#define RTD_MAX_COUNT           3
#define RTD_BREW_BOILER_IDX     0
#define RTD_BREW_HEAD_IDX       1
#define RTD_STEAM_BOILER_IDX    2


