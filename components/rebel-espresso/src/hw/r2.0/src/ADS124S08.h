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

/* Programmable excitation current range (IDACs) */
#define ADS124S08_IDAC_OFF           0b0000u
#define ADS124S08_IDAC_10uA          0b0001u
#define ADS124S08_IDAC_50uA          0b0010u
#define ADS124S08_IDAC_100uA         0b0011u
#define ADS124S08_IDAC_250uA         0b0100u
#define ADS124S08_IDAC_500uA         0b0101u
#define ADS124S08_IDAC_750uA         0b0110u
#define ADS124S08_IDAC_1000uA        0b0111u
#define ADS124S08_IDAC_1500uA        0b1000u
#define ADS124S08_IDAC_2000uA        0b1001u

/* Programmable Gain control */
#define ADS124S08_PGA_GAIN1             0b000u
#define ADS124S08_PGA_GAIN2             0b001u
#define ADS124S08_PGA_GAIN4             0b010u
#define ADS124S08_PGA_GAIN8             0b011u
#define ADS124S08_PGA_GAIN16            0b100u
#define ADS124S08_PGA_GAIN32            0b101u
#define ADS124S08_PGA_GAIN64            0b110u
#define ADS124S08_PGA_GAIN128           0b111u


struct ADS124S08_adc_mux_t {
    uint8_t mux_n : 4;
    uint8_t mux_p : 4;
};

struct ADS124S08_idac_mux_t {
    uint8_t mux_idac1 : 4;
    uint8_t mux_idac2 : 4;
};

struct ADS124S08_data_t {
    uint8_t status : 8;

    /**
     * Value converted into a voltage
     */
    double value;

    /**
     * Reference voltage used during conversion
     */
    double v_ref;
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
 * Set inpput MUX to ADC (AINx) as desired.
 * @param mux
 */
void ADS124S08_set_adc_mux(struct ADS124S08_adc_mux_t mux);
struct  ADS124S08_adc_mux_t ADS124S08_get_adc_mux();

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
 * See ADS124S08_IDAC_*
 */
void ADS124S08_set_idac_current(uint8_t delay);
uint8_t ADS124S08_get_idac_current();

/**
 * See ADS124S08_PGA_*
 */
void ADS124S08_set_pga_gain(uint8_t value);
uint8_t ADS124S08_get_pga_gain();

/**
 * Set inpput MUX to ADC (AINx) as desired.
 * @param mux
 */
void ADS124S08_set_idac_mux(struct ADS124S08_idac_mux_t mux);
struct  ADS124S08_idac_mux_t ADS124S08_get_idac_mux();

/**
 * Global chop mode, where AINN and AINNP are swapped over
 * in succession to zero out offset errors on conversions.
 * @param enable
 */
void ADS124S08_enable_chop(bool enable);
bool ADS124S08_is_chop_enabled();

/**
 * Do a one shot conversion and return the value.
 *
 * This is thread-safe
 *
 * @return Contains status and 24-bit value just read
 */
struct ADS124S08_data_t ADS124S08_conv(
        bool enable_chop,
        enum ADS124S08_ref_t ref,
        struct ADS124S08_adc_mux_t adc_mux,
        struct ADS124S08_idac_mux_t idac_mux,
        uint8_t idac_current, uint8_t pga_gain);

double ADS124S08_get_vref();
