#pragma once

#include <driver/spi_master.h>

/* Mux Definitions */
#define ADS124S08_MUX_AIN0      0b0000
#define ADS124S08_MUX_AIN1      0b0001
#define ADS124S08_MUX_AIN2      0b0010
#define ADS124S08_MUX_AIN3      0b0011
#define ADS124S08_MUX_AIN4      0b0100
#define ADS124S08_MUX_AIN5      0b0101
#define ADS124S08_MUX_AIN6      0b0110
#define ADS124S08_MUX_AIN7      0b0111
#define ADS124S08_MUX_AIN8      0b1000
#define ADS124S08_MUX_AIN9      0b1001
#define ADS124S08_MUX_AIN10     0b1010
#define ADS124S08_MUX_AIN11     0b1011
#define ADS124S08_MUX_AINCOM    0b1100

/* Programmable delay before taking a sample for settling */
#define ADS124S08_DELAY_4us         0b111
#define ADS124S08_DELAY_54us        0b000
#define ADS124S08_DELAY_97us        0b001
#define ADS124S08_DELAY_250us       0b010
#define ADS124S08_DELAY_1ms         0b011
#define ADS124S08_DELAY_4ms         0b100
#define ADS124S08_DELAY_8ms         0b101
#define ADS124S08_DELAY_16ms        0b110

struct ADS124S08_mux_t {
    uint8_t mux_n : 4;
    uint8_t mux_p : 4;
};

struct ADS124S08_data_t {
    uint8_t status : 8;
    double value;
};

enum ADS124S08_ref_t {
    ADS124S08_ref_INTERNAL,
    ADS124S08_ref_EXTERNAL
};

void ADS124S08_init(spi_host_device_t spi);

void ADS124S08_wakeup();
void ADS124S08_powerdown();
void ADS124S08_reset();
void ADS124S08_start();
void ADS124S08_stop();

/**
 * Set inpput and output MUX as desired.
 * @param mux
 */
void ADS124S08_set_mux(struct ADS124S08_mux_t mux);
struct  ADS124S08_mux_t ADS124S08_get_mux();

/**
 * Set voltage reference to be the internal 2.5V buffer or external reference
 * via REFN0 and REFP0
 * @param ref INTERNAL or EXTERNAL
 */
void ADS124S08_set_ref(enum ADS124S08_ref_t ref);
enum ADS124S08_ref_t ADS124S08_get_ref();

/**
 * See ADS124S08_DELAY_*
 */
void ADS124S08_set_conv_delay(uint8_t delay);
uint8_t ADS124S08_get_conv_delay();

/**
 * Do a one shot conversion and return the value.
 * @return Contains status and 24-bit value just read
 */
struct  ADS124S08_data_t ADS124S08_conv();
