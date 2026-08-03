#pragma once

#include <cstdint>
#include <cstddef>
#include <esp_err.h>

esp_err_t eeprom_read_stream(uint16_t data_addr, uint8_t *data, size_t size);

esp_err_t eeprom_write_stream(uint16_t start_addr, const uint8_t *data, size_t size);

esp_err_t eeprom_init();
