#include "eeprom.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <driver/i2c.h>
#include <sys/param.h>
#include "sdkconfig.h"
#include "hw_config.h"

#define TAG "eeprom"

#define ACK_CHECK_EN    0x1     /*!< I2C master will check ack from slave*/
#define ACK_VAL         I2C_MASTER_ACK      /*!< I2C ack value */
#define NACK_VAL        I2C_MASTER_NACK     /*!< I2C nack value */
#define PAGE_SIZE       32

#ifndef I2C_MASTER_NUM
#define I2C_MASTER_NUM          -1
#endif

#ifndef I2C_EEPROM_ADDRESS
#define I2C_EEPROM_ADDRESS      0xFFu
#endif

 esp_err_t eeprom_read_stream(uint16_t data_addr, uint8_t *data, size_t size) {
    uint8_t high_addr = data_addr >> 8u & 0xffu;
    uint8_t low_addr = data_addr & 0xffu;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, I2C_EEPROM_ADDRESS << 1u | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, high_addr, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, low_addr, ACK_CHECK_EN);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, I2C_EEPROM_ADDRESS << 1u | I2C_MASTER_READ, ACK_CHECK_EN);
    if (size > 1) {
        i2c_master_read(cmd, data, size - 1, I2C_MASTER_ACK);
    }
    i2c_master_read(cmd, data + size - 1, 1, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t _write_in_page(uint16_t data_addr, const uint8_t *data, size_t size) {
    uint8_t high_addr = data_addr >> 8u & 0xffu;
    uint8_t low_addr = data_addr & 0xffu;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, I2C_EEPROM_ADDRESS << 1u | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, high_addr, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, low_addr, ACK_CHECK_EN);
    i2c_master_write(cmd, data, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    vTaskDelay(200 / portTICK_PERIOD_MS);
    return ret;
}

/*
 * Writes a limited by page size, so they need to be stitched accordingly.
 */
esp_err_t eeprom_write_stream(uint16_t start_addr, const uint8_t *data, size_t size) {
    esp_err_t err = ESP_OK;
    size_t remaining = size;
    const uint8_t *ptr = data;
    uint16_t addr = start_addr;

    do {
        size_t len = MIN(PAGE_SIZE, remaining);

        // Make sure end address is aligned to page boundary, this is strictly required for the first write only.
        if (remaining > PAGE_SIZE && remaining == size) {
            size_t end_address = addr + len;
            end_address = PAGE_SIZE * (end_address / PAGE_SIZE);
            len = end_address - addr;
        }

        // Now do it
        err = _write_in_page(addr, ptr, len);
        addr += len;
        ptr += len;
        remaining -= len;
    } while (remaining != 0 && err == ESP_OK);

    return err;
}


