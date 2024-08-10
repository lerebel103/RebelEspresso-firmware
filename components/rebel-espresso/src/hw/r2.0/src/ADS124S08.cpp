#define TAG "ADS124S08"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <climits>
#include <esp_log.h>
#include <cstring>
#include <cmath>
#include <driver/gpio.h>
#include "ADS124S08.h"
#include "hw_config.h"

#define ADS124S08_INTERNAL_REF_VOLTAGE 2.5

/* Control Commands */
#define CMD_NOP         0b00000000u
#define CMD_WAKEUP      0b00000010u
#define CMD_POWERDOWN   0b00000100u
#define CMD_RESET       0b00000110u
#define CMD_START       0b00001000u
#define CMD_STOP        0b00001010u

/* Calibration Commands */
#define CMD_SYOCAL      0b00010110u
#define CMD_SYGCAL      0b00010111u
#define CMD_SFOCAL      0b00011001u

/* Data Read Command */
#define CMD_RDATA       0b00010010u

/* Register Read and Write Commands */
#define CMD_RREG        0b00100000u
#define CMD_WREG        0b01000000u

/* Config Registers */
#define REG_ID          0x00u
#define REG_STATUS      0x01u
#define REG_INPMUX      0x02u
#define REG_PGA         0x03u
#define REG_DATARATE    0x04u
#define REG_REF         0x05u
#define REG_IDACMAG     0x06u
#define REG_IDACMUX     0x07u
#define REG_VBIAS       0x08u
#define REG_SYS         0x09u
#define REG_OFCAL0      0x0Au
#define REG_OFCAL1      0x0Bu
#define REG_OFCAL2      0x0Cu
#define REG_FSCAL0      0x0Du
#define REG_FSCAL1      0x0Eu
#define REG_FSCAL2      0x0Fu
#define REG_GPIODAT     0x10u
#define REG_GPIOCON     0x11u


static spi_device_handle_t s_device_handle;

static SemaphoreHandle_t s_conv_lock = nullptr;
static ADS124S08_ref_t s_ref_type;
static double s_vRef = 0;
static double s_pga_gain = 1;
static bool s_chop_enabled = false;

static esp_err_t _write_cmd(uint8_t cmd) {
    spi_transaction_ext_t transaction = {};
    transaction.base.length = 8;
    transaction.base.rxlength = 0;
    transaction.base.cmd = cmd;
    transaction.base.tx_buffer = nullptr;
    transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    transaction.command_bits = 8;
    transaction.address_bits = 0;

    esp_err_t err = spi_device_transmit(s_device_handle, &transaction.base);

    return err;
}

static esp_err_t _read_register(uint8_t addr, uint8_t *result, uint8_t size) {
    assert(size <= 4);  // we're using the transaction buffers
    spi_transaction_ext_t transaction = {};
    transaction.base.length = CHAR_BIT * (2 + size);
    transaction.base.rxlength = CHAR_BIT * size;
    transaction.base.cmd = ((CMD_RREG | addr) << 8u) | (size - 0x01u);  /* n - 1 reads */
    transaction.base.tx_buffer = nullptr;
    transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_USE_RXDATA;
    transaction.command_bits = 16;
    transaction.address_bits = 0;

    esp_err_t err = ESP_FAIL;
    err = spi_device_transmit(s_device_handle, &transaction.base);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error sending SPI transaction: %s", esp_err_to_name(err));
        return err;
    }
    memcpy(result, transaction.base.rx_data, size);

    return err;
}

static esp_err_t _write_register(uint8_t addr, uint8_t *data, uint8_t size) {
    assert(size <= 4);  // we're using the transaction buffers
    spi_transaction_ext_t transaction = {};
    transaction.base.length = (0 + size) * CHAR_BIT;
    transaction.base.cmd = ((CMD_WREG | addr) << 8u) | (size - 0x01u);  /* n - 1 reads */
    memcpy(transaction.base.tx_data, data, size);
    transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    transaction.command_bits = 16;
    transaction.address_bits = 0;

    if (size == 0) {
        transaction.base.tx_buffer = nullptr;
    } else {
        transaction.base.flags |= SPI_TRANS_USE_TXDATA;
    }

    esp_err_t err = ESP_FAIL;
    err = spi_device_transmit(s_device_handle, &transaction.base);
    return err;
}

static esp_err_t _read_data(uint8_t *status, uint32_t *value) {
    DMA_ATTR static uint8_t data[4];
    spi_transaction_ext_t transaction = {};
    transaction.base.length = CHAR_BIT * (1 + 4);
    transaction.base.rxlength = CHAR_BIT * 4;
    transaction.base.cmd = CMD_RDATA;
    transaction.base.tx_buffer = nullptr;
    transaction.base.rx_buffer = &data;
    transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    transaction.command_bits = 8;
    transaction.address_bits = 0;

    esp_err_t err = ESP_FAIL;
    err = spi_device_transmit(s_device_handle, &transaction.base);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error sending SPI transaction: %s", esp_err_to_name(err));
        return err;
    }

    *status = data[0];
    *value = data[3];
    *value |= ((uint32_t) data[2]) << 8u;
    *value |= ((uint32_t) data[1]) << 16u;

    return err;
}

void ADS124S08_wakeup() {
    _write_cmd(CMD_WAKEUP);
}

void ADS124S08_powerdown() {
    _write_cmd(CMD_POWERDOWN);
}

void ADS124S08_reset() {
    // Here we go straight to the reset pin (safer than SPI commands)
    ESP_LOGI(TAG, "  >> RESET <<");
    gpio_set_level(PIN_OUT_ADC_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(2));

    // Must wait after a reset
    gpio_set_level(PIN_OUT_ADC_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void ADS124S08_start() {
    _write_cmd(CMD_START);
}

void ADS124S08_stop() {
    _write_cmd(CMD_STOP);
}

void ADS124S08_set_adc_mux(struct ADS124S08_adc_mux_t mux) {
    uint8_t write = mux.mux_p;
    write = (write << 4u) | mux.mux_n;
    _write_register(REG_INPMUX, &write, 1);
}

struct ADS124S08_adc_mux_t ADS124S08_get_adc_mux() {
    // Read AIN Neg and Pos separately
    uint8_t read;
    ADS124S08_adc_mux_t val = {};
    _read_register(REG_INPMUX, &read, 1);
    val.mux_n = read & 0b00001111;
    val.mux_p = (read >> 4u) & 0b00001111;
    return val;
}

void ADS124S08_set_idac_mux(struct ADS124S08_idac_mux_t mux) {
    uint8_t write = mux.mux_idac1;
    write = (write << 4u) | mux.mux_idac2;
    _write_register(REG_IDACMUX, &write, 1);
}

struct ADS124S08_idac_mux_t ADS124S08_get_idac_mux() {
    // Read AIN Neg and Pos separately
    uint8_t read;
    ADS124S08_idac_mux_t val = {};
    _read_register(REG_IDACMUX, &read, 1);
    val.mux_idac1 = read & 0b00001111;
    val.mux_idac2 = (read >> 4u) & 0b00001111;
    return val;
}

double ADS124S08_get_vref() {
    double vRef = 0;
    if (s_ref_type == ADS124S08_ref_INTERNAL) {
        vRef = ADS124S08_INTERNAL_REF_VOLTAGE;
    } else if (s_ref_type == ADS124S08_ref_EXTERNAL) {
        // Calculate voltage reference based on selected IDAC current (ADAC1 + IDAC2)
        vRef = s_vRef;
    } else {
        ESP_LOGE(TAG, "No idea what VREF is set to");
    }
    return vRef;
}

void ADS124S08_enable_chop(bool enable) {
    uint8_t val;
    _read_register(REG_DATARATE, &val, 1);
    val &= ~0b10000000u; // Turn off CHOP
    if (enable) {
        val |= 0b10000000u;
    }
    _write_register(REG_DATARATE, &val, 1);
    s_chop_enabled = enable;
}

bool ADS124S08_is_chop_enabled() {
    uint8_t val;
    _read_register(REG_DATARATE, &val, 1);
    return val & 0b10000000u;
}


struct ADS124S08_data_t ADS124S08_conv(
        bool enable_chop,
        enum ADS124S08_ref_t ref,
        struct ADS124S08_adc_mux_t adc_mux,
        struct ADS124S08_idac_mux_t idac_mux,
        uint8_t idac_current, uint8_t pga_gain) {
    struct ADS124S08_data_t data = {};
    data.status = -1;

    // Can only do one conversion at a time
    if( xSemaphoreTake(s_conv_lock, MAX_SPI_WAIT_TICKS) == pdTRUE)
    {
        // Always reset status flag
        uint8_t val = 0;
        _write_register(REG_STATUS, &val, 1);

        // Set all params before single shot conversion is started
        ADS124S08_set_ref(ref);
        ADS124S08_set_adc_mux(adc_mux);
        ADS124S08_set_idac_mux(idac_mux);
        ADS124S08_set_idac_current(idac_current);
        ADS124S08_set_pga_gain(pga_gain);
        ADS124S08_enable_chop(enable_chop);

        // Go - Do conversion
        ADS124S08_start();

        // Delay calculated for 20FPS, LL filter, conv delay + CHOP as required with margin
        auto delay = (60 + 10);
        if (enable_chop) {
            delay *= 2;
        }
        vTaskDelay(pdMS_TO_TICKS(delay));

        // Now read register back, it will contain status and 24-bit value
        uint8_t status;
        uint32_t value;
        _read_data(&status, &value);

        // Work out which reference voltage was used and convert back accordingly
        double vRef = ADS124S08_get_vref() / s_pga_gain;
        double offset = 0;

        // Convert digital value into analog range one
        double lsb = 2 * vRef / (1u << 24u);
        double full_scale_analog = vRef - lsb;
        data.value = (value - offset) * full_scale_analog / 0x7FFFFF;
        data.status = status;
        data.v_ref = s_vRef;

        xSemaphoreGive(s_conv_lock);
    }

    return data;
}

struct ADS124S08_data_t ADS124S08_internal_temp() {
    struct ADS124S08_data_t data = {};
    data.status = -1;

    // Can only do one conversion at a time
    if( xSemaphoreTake(s_conv_lock, MAX_SPI_WAIT_TICKS) == pdTRUE)
    {
        // Always reset status flag
        uint8_t val = 0;
        _write_register(REG_STATUS, &val, 1);

        // Gain must be 4 at most
        ADS124S08_set_pga_gain(ADS124S08_PGA_GAIN4);
        ADS124S08_set_ref(ADS124S08_ref_INTERNAL);

        auto delay = (5);
        vTaskDelay(pdMS_TO_TICKS(delay));

        // Enable internal temperature sensor
        uint8_t reg_sys = 0;
        _read_register(REG_SYS, &reg_sys, 1);
        uint8_t new_val = reg_sys;
        new_val &= ~0b11100000u;
        new_val |=  0b01000000u;
        _write_register(REG_SYS, &new_val, 1);

        // Set high DR, don't care
        uint8_t reg_dr;
        _read_register(REG_DATARATE, &reg_dr, 1);
        new_val = reg_dr;
        new_val &= ~0b10001111u;
        new_val |= 0b00001001u;
        _write_register(REG_DATARATE, &new_val, 1);

        // Go - Do conversion
        ADS124S08_start();
        delay = 20;
        vTaskDelay(pdMS_TO_TICKS(delay));

        // Now read register back, it will contain status and 24-bit value
        uint8_t status;
        uint32_t value;
        _read_data(&status, &value);

        // Now put back old value for registers
        _write_register(REG_SYS, &reg_sys, 1);
        _write_register(REG_DATARATE, &reg_dr, 1);

        double vRef = ADS124S08_get_vref() / s_pga_gain;
        double offset = 0;

        // Convert digital value into analog range one
        // For internal temperature:
        // 25 -> 129mV
        // 403 uV per C
        double lsb = 2 * vRef / (1u << 24u);
        double full_scale_analog = vRef - lsb;
        data.value = (value - offset) * full_scale_analog / 0x7FFFFF;
        static double rate = 403 * 1e-6;
        data.value = (data.value - 0.129) / rate + 25;
        data.status = status;
        data.v_ref = s_vRef;

        if (status != 0) {
            printf("\r\n\r\nError reading internal temperature !!!!!!!!!!!!!!!\r\n\r\n");
        }

        xSemaphoreGive(s_conv_lock);
    }

    return data;

}


void ADS124S08_set_ref(enum ADS124S08_ref_t ref) {
    uint8_t val;
    _read_register(REG_REF, &val, 1);

    if (ref == ADS124S08_ref_INTERNAL) {
        val &= ~0b00001100u;
        val |= 0b00001000u;
        s_ref_type = ADS124S08_ref_INTERNAL;
        ESP_LOGD(TAG, "Using Internal 2.5V Reference");
    } else if (ref == ADS124S08_ref_EXTERNAL) {
        val &= ~0b00001100u;
        ESP_LOGD(TAG, "Using External REFN0 - REFP0 as Reference");
        s_ref_type = ADS124S08_ref_EXTERNAL;
    }

    _write_register(REG_REF, &val, 1);
}

enum ADS124S08_ref_t ADS124S08_get_ref() {
    uint8_t val;
    _read_register(REG_REF, &val, 1);

    if (val & 0b00001000u) {
        return ADS124S08_ref_INTERNAL;
    } else {
        return ADS124S08_ref_EXTERNAL;
    }
}

void ADS124S08_set_idac_current(uint8_t value) {
    uint8_t val;
    _read_register(REG_IDACMAG, &val, 1);

    val &= ~0b00001111u;
    val |= value;
    _write_register(REG_IDACMAG, &val, 1);

    // Calculate VREF based on this
    if (value == ADS124S08_IDAC_OFF) {
        s_vRef = ADC_RREF;
    } else if (value == ADS124S08_IDAC_10uA) {
        s_vRef = ADC_RREF * 10e-6;
    } else if (value == ADS124S08_IDAC_50uA) {
        s_vRef = ADC_RREF * 50e-6;
    } else if (value == ADS124S08_IDAC_100uA) {
        s_vRef = ADC_RREF * 100e-6;
    } else if (value == ADS124S08_IDAC_250uA) {
        s_vRef = ADC_RREF * 250e-6;
    } else if (value == ADS124S08_IDAC_500uA) {
        s_vRef = ADC_RREF * 500e-6;
    } else if (value == ADS124S08_IDAC_750uA) {
        s_vRef = ADC_RREF * 750e-6;
    } else if (value == ADS124S08_IDAC_1000uA) {
        s_vRef = ADC_RREF * 1000e-6;
    } else if (value == ADS124S08_IDAC_1500uA) {
        s_vRef = ADC_RREF * 1500e-6;
    } else if (value == ADS124S08_IDAC_2000uA) {
        s_vRef = ADC_RREF * 2000e-6;
    }

    s_vRef = s_vRef * 2;
}

uint8_t ADS124S08_get_idac_current() {
    uint8_t val;
    _read_register(REG_IDACMAG, &val, 1);
    return val & 0b00001111u;
}

void ADS124S08_set_conv_delay(uint8_t delay) {
    uint8_t val;
    _read_register(REG_PGA, &val, 1);

    // DELAY[7:5]
    // GAIN[4:3]
    // PGA_EN[2:0]
    val &= ~0b11100000u;
    val |= (delay << 5u);

    _write_register(REG_PGA, &val, 1);
}

uint8_t ADS124S08_get_conv_delay() {
    uint8_t val;
    _read_register(REG_PGA, &val, 1);

    return (val & 0b11100000u) >> 5u;
}

void ADS124S08_set_pga_gain(uint8_t value) {
    uint8_t val;
    _read_register(REG_PGA, &val, 1);

    // DELAY[7:5]
    // GAIN[4:3]
    // PGA_EN[2:0]
    val &= ~0b00011111u;
    val |= value;

    // Record gain to help with conversions later
    if (value == ADS124S08_PGA_GAIN1) {
        s_pga_gain = 1;
    } else if (value == ADS124S08_PGA_GAIN2) {
        s_pga_gain = 2;
    } else if (value == ADS124S08_PGA_GAIN4) {
        s_pga_gain = 4;
    } else if (value == ADS124S08_PGA_GAIN8) {
        s_pga_gain = 8;
    } else if (value == ADS124S08_PGA_GAIN16) {
        s_pga_gain = 16;
    } else if (value == ADS124S08_PGA_GAIN32) {
        s_pga_gain = 32;
    } else if (value == ADS124S08_PGA_GAIN64) {
        s_pga_gain = 64;
    } else if (value == ADS124S08_PGA_GAIN128) {
        s_pga_gain = 128;
    }

    if (s_pga_gain > 1) {
        // Enable PGA only if a gain is desired.
        val |= 0b00001000u;
    }

    // Set new gain, with enable flag as needed
    _write_register(REG_PGA, &val, 1);
}

uint8_t ADS124S08_get_pga_gain() {
    uint8_t val;
    _read_register(REG_PGA, &val, 1);
    return val & 0b00000111u;
}


void _configure() {// RESET status, POR event would have occurred and needs to be cleared
    uint8_t val = 0;
    uint8_t new_val = 0;

    ADS124S08_reset();

    // Reset POR flag immediately after a reset
    val = 0;
    _write_register(REG_STATUS, &val, 1);

    /*
         7 CHOP: 1 -> Enabled
         6 CLK: 0 -> Internal CLK
         5 Mode: 1 -> Single Shot
         4 Filter: -> 1 Low latency
         3-0: datarate
     */
    new_val = 0b00110100;
    _write_register(REG_DATARATE, &new_val, 1);
    _read_register(REG_DATARATE, &val, 1);
    ESP_ERROR_CHECK((val == new_val ? ESP_OK : ESP_ERR_INVALID_STATE));

    // Sys register
    /*
        7:5	SYS_MON[2:0]
        4:3	CAL_SAMP[1:0] (10 is 8 samples)
        2	TIMEOUT
        1	CRC
        0	SENDSTAT
     */
    new_val = 0b00010001;
    _write_register(REG_SYS, &new_val, 1);
    _read_register(REG_SYS, &val, 1);
    ESP_ERROR_CHECK((val == new_val ? ESP_OK : ESP_ERR_INVALID_STATE));

    // VBIAS
    new_val = 0b00000000;
    _write_register(REG_VBIAS, &new_val, 1);
    _read_register(REG_VBIAS, &val, 1);
    ESP_ERROR_CHECK((val == new_val ? ESP_OK : ESP_ERR_INVALID_STATE));

    // Internal reference, set to always on
    /*
        7:6	FL_REF_EN[1:0]
        5	REFP_BUF
        4	REFN_BUF
        3:2	REFSEL[1:0]
        1:0	REFCON[1:0]
     */
    new_val = 0b01000010;
    _write_register(REG_REF, &new_val, 1);
    _read_register(REG_REF, &val, 1);
    ESP_ERROR_CHECK((val == new_val ? ESP_OK : ESP_ERR_INVALID_STATE));
    s_ref_type = ADS124S08_get_ref() == ADS124S08_ref_INTERNAL ? ADS124S08_ref_INTERNAL : ADS124S08_ref_EXTERNAL;

    // Set conversion delay to something reasonable for settling time
    ADS124S08_set_conv_delay(ADS124S08_DELAY_1ms);
    // Turn off IDAC by default
    ADS124S08_set_idac_current(ADS124S08_IDAC_OFF);
    // No chop by default
    ADS124S08_enable_chop(false);

    // Do self-offset calibration
    ADS124S08_wakeup();
    _write_register(CMD_SFOCAL, &new_val, 0);

    ESP_LOGI(TAG, "  >> CONFIGURED <<");
}

void ADS124S08_init(spi_host_device_t spi) {
    if (s_conv_lock != nullptr) {
        ESP_LOGE(TAG, "Already initialised");
    }

    s_conv_lock = xSemaphoreCreateMutex();

    // Setup RESET pin as output
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (
            (1ULL << PIN_OUT_ADC_RESET)
    );
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Pull GPIO high for RESET
    gpio_set_level(PIN_OUT_ADC_RESET, 1);

    // Add this device to the SPI bus
    spi_device_interface_config_t deviceConfig = {};
    deviceConfig.spics_io_num = PIN_OUT_ADC_CS;
    deviceConfig.clock_speed_hz = SPI_MASTER_FREQ_20M;
    deviceConfig.mode = 3;
    deviceConfig.address_bits = 0;
    deviceConfig.command_bits = 0;
    deviceConfig.dummy_bits = 0;
    deviceConfig.flags = SPI_DEVICE_HALFDUPLEX;
    deviceConfig.queue_size = 1;
    deviceConfig.cs_ena_pretrans = 1;
    deviceConfig.cs_ena_posttrans = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(spi, &deviceConfig, &s_device_handle));

    _configure();
}