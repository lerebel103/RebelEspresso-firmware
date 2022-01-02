#pragma once

#include <driver/adc.h>

// SPI bus
#define PIN_MISO                GPIO_NUM_12
#define PIN_MOSI                GPIO_NUM_13
#define PIN_SCK                 GPIO_NUM_14

// RTD handling
#define PIN_OUT_ADC_CS          GPIO_NUM_15
#define PIN_OUT_RTD_A0          GPIO_NUM_18
#define PIN_OUT_RTD_A1          GPIO_NUM_19

// TEC
#define PIN_OUT_HBRIDGE_PWM     GPIO_NUM_17
#define PIN_OUT_HBRIDGE_DIR     GPIO_NUM_23
#define PIN_OUT_HBRIDGE_DIS     GPIO_NUM_0
#define PIN_IN_HBRIDGE_SO       GPIO_NUM_39

// outputs
#define PIN_OUT_SSR1            GPIO_NUM_21
#define PIN_OUT_REL1_EN         GPIO_NUM_25
#define PIN_OUT_REL2_EN         GPIO_NUM_26
#define PIN_OUT_REL3_EN         GPIO_NUM_27

// Switches
#define PIN_IN_SYS_EN           GPIO_NUM_2
#define PIN_IN_BREW_EN          GPIO_NUM_35
#define PIN_IN_STEAM_EN         GPIO_NUM_34

// Water sensing
#define PIN_WATER_LEVEL_ENABLE  GPIO_NUM_32
#define PIN_WATER_LEVEL_SENSE   ADC1_CHANNEL_0

// OLED
#define PIN_OUT_DISPLAY_DC      GPIO_NUM_22
#define PIN_OUT_DISPLAY_CS      GPIO_NUM_33
#define PIN_OUT_ADC_RESET       GPIO_NUM_NC

// Reset button, hard wired to bootloader via sdkconfig
#define PIN_IN_RESET            GPIO_NUM_4

#define RTD_R_NOMINAL       1000.0f
#define RTD_R_REF           4020.0f

#define RTD_MAX_COUNT           5
#define RTD_INTERNAL_IDX        0
#define RTD_BREW_BOILER_IDX     1
#define RTD_BREW_HEAD_IDX       2
#define RTD_TEC_HOT_IDX         3
#define RTD_TEC_COLD_IDX        4

#define WATER_LEVEL_SENSE_ON   1
#define WATER_LEVEL_SENSE_OFF  0
