#pragma once

#include <driver/adc.h>

// SPI bus
#define GPIO_MISO       GPIO_NUM_12
#define GPIO_MOSI       GPIO_NUM_13
#define GPIO_SCK        GPIO_NUM_14

// RTD handling
#define GPIO_RTD_CS     GPIO_NUM_15
#define GPIO_RTD_A0     GPIO_NUM_18
#define GPIO_RTD_A1     GPIO_NUM_19

#define RTD_R_NOMINAL       1000.0f
#define RTD_R_REF           4020.0f
#define RTD_MAX_COUNT       4
#define RTD_BOILER_IDX      0
#define RTD_BREW_HEAD_IDX   1
#define RTD_TEC_HOT_IDX     2
#define RTD_TEC_COLD_IDX    3

// outputs
#define GPIO_TRIG1_SSR      GPIO_NUM_21
#define GPIO_TRIG2_REL1     GPIO_NUM_25
#define GPIO_TRIG2_REL2     GPIO_NUM_26
#define GPIO_TRIG2_REL3     GPIO_NUM_27
