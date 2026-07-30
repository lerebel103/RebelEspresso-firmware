#include "eeprom.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <sys/param.h>
#include <driver/i2c_master.h>
#include <cstring>
#include "sdkconfig.h"
#include "hw_config.h"
#include "hw_specs.h"

#define TAG "eeprom"

#define PAGE_SIZE       32

#ifndef I2C_MASTER_NUM
#define I2C_MASTER_NUM          -1
#endif

#ifndef I2C_EEPROM_ADDRESS
#define I2C_EEPROM_ADDRESS      0xFFu
#endif

static i2c_master_dev_handle_t dev_handle;


esp_err_t eeprom_read_stream(uint16_t data_addr, uint8_t *data, size_t size) {
  uint8_t high_addr = data_addr >> 8u & 0xffu;
  uint8_t low_addr = data_addr & 0xffu;

  uint8_t write_buffer[] = {high_addr, low_addr};
  return i2c_master_transmit_receive(dev_handle, write_buffer, 2, data, size, 1000);
}

static esp_err_t _write_in_page(uint16_t data_addr, const uint8_t *data, size_t size) {
  uint8_t high_addr = data_addr >> 8u & 0xffu;
  uint8_t low_addr = data_addr & 0xffu;

  const size_t write_size = size + 2;
  auto *write_buffer = new uint8_t[write_size];
  write_buffer[0] = high_addr;
  write_buffer[1] = low_addr;
  memcpy(write_buffer + 2, data, size);

  esp_err_t ret = i2c_master_transmit(dev_handle, write_buffer, write_size, 1000);

  delete[] write_buffer;

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

esp_err_t eeprom_init() {
  // Add our device handle
  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = I2C_EEPROM_ADDRESS,
      .scl_speed_hz = 100000,
      .scl_wait_us = 0,
      .flags = {0},
  };

  i2c_master_bus_handle_t bus_handle = hw_specs_get_i2c_handle();
  return i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
}


