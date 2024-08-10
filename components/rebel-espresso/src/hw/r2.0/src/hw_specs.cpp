#include <src/hw/base/boiler_temp.h>
#include <src/hw/base/brew_temp.h>
#include <hal/i2c_types.h>
#include <driver/i2c.h>
#include <esp_log.h>
#include <src/hw/base/out_signals.h>
#include <driver/spi_common.h>

#include "hw_specs.h"
#include "hw_config.h"
#include "ADS124S08.h"
#include "eeprom.h"

#define TAG "hw_specs"

#define I2C_MASTER_TX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define READ_BIT I2C_MASTER_WRITE  /*!< I2C master write */
#define WRITE_BIT I2C_MASTER_READ    /*!< I2C master read */
#define ACK_CHECK_EN 0x1            /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0           /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                 /*!< I2C ack value */
#define NACK_VAL 0x1                /*!< I2C nack value */


void _print_i2c_devices() {
    uint8_t address;
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (address << 1) | READ_BIT, ACK_CHECK_EN);
            i2c_master_stop(cmd);
            esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                printf("%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("UU ");
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
}

void hw_specs_read_water_level_mv(uint8_t *status, double *value) {
    // Set mux for water level read
    ADS124S08_adc_mux_t mux = {
            .mux_n  = ADS124S08_MUX_AIN3,
            .mux_p = ADS124S08_MUX_AINCOM
    };
    ADS124S08_idac_mux_t idac_mux = {
            .mux_idac1 = ADS124S08_MUX_AIN0,
            .mux_idac2 = ADS124S08_MUX_AIN1,
    };
    double idac_current = ADS124S08_IDAC_OFF;
    double gain = ADS124S08_PGA_GAIN1;

    // Go
    ADS124S08_data_t data = ADS124S08_conv(false, ADS124S08_ref_INTERNAL, mux, idac_mux, idac_current, gain);
    *status = data.status;
    *value = data.value * 1000;

    // Detect short circuit condition, below 100mV
    if (*value < 100) {
        ESP_LOGE(TAG, "Short in water level: %fmV", *value);
        *status |= 0b00000010u;
    }

    // Can also be over internal reference voltage (wrap around)
    if (*value > 2500) {
        ESP_LOGE(TAG, "Short in water level (over ref voltage): %fmV", *value);
        *status |= 0b00000010u;
    }
}

bool hw_specs_is_aux_in_activated() {
    return gpio_get_level(PIN_IN_AUX_EN) == 0;
}



void hw_specs_init(esp_event_loop_handle_t event_loop) {
    // I2C Initialisation
    i2c_config_t conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = I2C_PIN_SDA,
            .scl_io_num = I2C_PIN_SCL,
            .sda_pullup_en = GPIO_PULLUP_DISABLE,
            .scl_pullup_en = GPIO_PULLUP_DISABLE,
            .master = {.clk_speed = I2C_MASTER_FREQ_HZ},
            .clk_flags =  I2C_SCLK_SRC_FLAG_FOR_NOMAL
    };

    i2c_param_config(I2C_MASTER_NUM, &conf);

    ESP_ERROR_CHECK(
            i2c_driver_install(I2C_MASTER_NUM, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0));
    //_print_i2c_devices();

    // Now can init I2C-dependent peripherals
    out_signals_init();

    // Aux input pin
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (
            (1ULL << PIN_IN_AUX_EN)
    );

    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
}


void hw_specs_cfg_to_json(cJSON *root, const char *base_key) {

}

void hw_specs_status_to_json(cJSON *root, const char *base_key) {

}

void hw_specs_handle_new_cfg(const cJSON *cfg) {

}

void hw_specs_handle_new_temp(uint64_t time_us, const reading_t &data, uint8_t idx) {
    switch (idx) {
        case RTD_BREW_BOILER_IDX:
            boiler_temp_process(time_us, data);
            break;
        case RTD_BREW_HEAD_IDX:
            brew_temp_process(time_us, data);
            break;
        case RTD_STEAM_BOILER_IDX:
            // Not yet implemented
            // steam_temp_process(time_us, data);
            break;
    }
}
