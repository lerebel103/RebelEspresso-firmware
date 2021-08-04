#include <src/hw/base/boiler_temp.h>
#include <src/hw/base/brew_temp.h>
#include <hal/i2c_types.h>
#include <driver/i2c.h>

#include "hw_specs.h"
#include "hw_config.h"

#define I2C_MASTER_FREQ_HZ 400000
#define I2C_MASTER_TX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */

void hw_specs_init(esp_event_loop_handle_t event_loop) {
    // Initialise I2C bus
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = PIN_SDA,
            .scl_io_num = PIN_SCL,
            .sda_pullup_en = GPIO_PULLUP_DISABLE,
            .scl_pullup_en = GPIO_PULLUP_DISABLE,
            .master = {.clk_speed = I2C_MASTER_FREQ_HZ}
    };

    i2c_param_config(i2c_master_port, &conf);

    ESP_ERROR_CHECK(
            i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0));
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
