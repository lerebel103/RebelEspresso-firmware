#pragma once

#include <driver/adc.h>

#define GPIO_MISO       GPIO_NUM_12
#define GPIO_MOSI       GPIO_NUM_13
#define GPIO_SCK        GPIO_NUM_14

// Temperature IC
#define GPIO_RTD_CS     GPIO_NUM_15

// Temp multiplexer
#define GPIO_RTD_A0     GPIO_NUM_18
#define GPIO_RTD_A1     GPIO_NUM_19

// Relay and SSR
#define GPIO_TRIG1      GPIO_NUM_21
#define GPIO_TRIG2      GPIO_NUM_25
#define GPIO_TRIG3      GPIO_NUM_26
#define GPIO_TRIG4      GPIO_NUM_27

// H-Bridge
#define GPIO_HBRIDGE_DIS      GPIO_NUM_2
#define GPIO_HBRIDGE_PWM      GPIO_NUM_17
#define GPIO_HBRIDGE_DIR      GPIO_NUM_23
#define GPIO_HBRIDGE_SO       GPIO_NUM_22

// Switches
#define GPIO_SW1      GPIO_NUM_33
#define GPIO_SW2      GPIO_NUM_34
#define GPIO_SW3      GPIO_NUM_0

// Navigation
#define GPIO_BTN_HOME       GPIO_NUM_4
#define GPIO_BTN_DOWN       GPIO_NUM_5
#define GPIO_BTN_UP         GPIO_NUM_16


// Water level
#define GPIO_SEN_PW             GPIO_NUM_32
#define GPIO_SEN_W_LEVEL        GPIO_NUM_36




