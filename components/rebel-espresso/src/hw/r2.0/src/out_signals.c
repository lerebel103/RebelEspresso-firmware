#include "out_signals.h"

#include <esp_log.h>
#include <driver/i2c.h>
#include "sdkconfig.h"
#include "hw_config.h"

#define TAG "out_signals"

#define IO_EXPANDER_ADDRESS 0x41u
#define ACK_CHECK_EN 1u
#define ACK_VAL 0x0u                             /*!< I2C ack value */
#define NACK_VAL 0x1u                            /*!< I2C nack value */
#define WRITE_BIT I2C_MASTER_WRITE
#define READ_BIT I2C_MASTER_READ

#define REGISTER_OUT 0x01
#define REGISTER_IN  0x00
#define REGISTER_CFG 0x03

/**
 *        the data will be stored in slave buffer.
 *        We can read them out from slave buffer.
 *
 * ___________________________________________________________________
 * | start | slave_addr + wr_bit + ack | write n bytes + ack  | stop |
 * --------|---------------------------|----------------------|------|
 *
 */
static esp_err_t _write_slave(uint8_t *data_wr, size_t size)
{
    i2c_port_t i2c_num = I2C_MASTER_NUM;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, (IO_EXPANDER_ADDRESS << 1) | WRITE_BIT, ACK_CHECK_EN));
    ESP_ERROR_CHECK(i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t _read_slave(uint8_t *data_rd, size_t size) {
    i2c_port_t i2c_num = I2C_MASTER_NUM;
    if (size == 0) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (IO_EXPANDER_ADDRESS << 1) | READ_BIT, ACK_CHECK_EN);
    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}


void out_signals_set_level(enum out_signals_t slot, uint8_t level) {
    // Read current state so we can or the desired pin output state
    static uint8_t state[] = {REGISTER_IN, 0x0};
    _read_slave(state, 2);

    if (slot == OUT_SIGNALS_RELAY1) {
        uint8_t set_output_cmd[] = {REGISTER_OUT, (level ? state[1] | 0x01u : state[1] & ~0x1u)};
        ESP_ERROR_CHECK(_write_slave(set_output_cmd, 2) );
    } else if (slot == OUT_SIGNALS_RELAY2) {
        uint8_t set_output_cmd[] = {REGISTER_OUT, (level ? state[1] | 0x02u : state[1] & ~0x2u)};
        ESP_ERROR_CHECK(_write_slave(set_output_cmd, 2) );
    } else if (slot == OUT_SIGNALS_RELAY3) {
        uint8_t set_output_cmd[] = {REGISTER_OUT, (level ? state[1] | 0x04u : state[1] & ~0x4u)};
        ESP_ERROR_CHECK(_write_slave(set_output_cmd, 2) );
    } else if (slot == OUT_SIGNALS_AUX) {
        uint8_t set_output_cmd[] = {REGISTER_OUT, (level ? state[1] | 0x08u : state[1] & ~0x8u)};
        ESP_ERROR_CHECK(_write_slave(set_output_cmd, 2) );
    } else {
        // Unsupported
        ESP_LOGE(TAG, "Slot %d not supported", slot);
    }
}

uint8_t out_signals_get_level(enum out_signals_t slot) {
    // Read current state so we can or the desired pin output state
    static uint8_t state[] = {REGISTER_IN, 0x0};
    _read_slave(state, 2);

    uint8_t val = 0;

    if (slot == OUT_SIGNALS_RELAY1) {
        val = (uint8_t)(state[1] >> 0u) & 0x1u;
    } else if (slot == OUT_SIGNALS_RELAY2) {
        val = (uint8_t)(state[1] >> 1u) & 0x1u;
    } else if (slot == OUT_SIGNALS_RELAY3) {
        val = (uint8_t)(state[1] >> 2u) & 0x1u;
    } else if (slot == OUT_SIGNALS_AUX) {
        val = (uint8_t)(state[1] >> 3u) & 0x1u;
    } else {
        // Unsupported
        ESP_LOGE(TAG, "Slot %d not supported", slot);
    }

    return val;
}

void out_signals_init() {
    // Configure pins as output, all of them
    uint8_t init_output_cmd[] = {REGISTER_CFG, 0x0};
    ESP_ERROR_CHECK(_write_slave(init_output_cmd, 2) );

    // Force all pins to low level
    uint8_t set_output_cmd[] = {REGISTER_OUT, 0x0};
    ESP_ERROR_CHECK(_write_slave(set_output_cmd, 2) );
    ESP_LOGI(TAG, "IO Expander initialised");
}

