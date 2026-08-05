#include "out_signals.h"

#include "hw_specs.h"
#include <esp_log.h>
#include <driver/i2c_master.h>
#include "sdkconfig.h"
#include "hw_config.h"

#define TAG "out_signals"

#define REGISTER_OUT 0x01
#define REGISTER_IN 0x00
#define REGISTER_CFG 0x03

static i2c_master_dev_handle_t dev_handle;

void out_signals_set_level(enum out_signals_t slot, uint8_t level) {
  // Read current state so we can OR in the desired pin. On any I2C error, log
  // and bail rather than abort — the I/O scan re-drives every output each 20ms
  // cycle, so a transient bus timeout must never panic the machine.
  uint8_t state[] = {REGISTER_IN, 0x0};
  esp_err_t err = i2c_master_transmit_receive(dev_handle, state, 1, state + 1, 1, 1000);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "IO expander read failed: %s", esp_err_to_name(err));
    return;
  }

  uint8_t mask;
  if (slot == OUT_SIGNALS_RELAY1) {
    mask = 0x01u;
  } else if (slot == OUT_SIGNALS_RELAY2) {
    mask = 0x02u;
  } else if (slot == OUT_SIGNALS_RELAY3) {
    mask = 0x04u;
  } else if (slot == OUT_SIGNALS_AUX) {
    mask = 0x08u;
  } else {
    ESP_LOGE(TAG, "Slot %d not supported", slot);
    return;
  }

  uint8_t set_output_cmd[] = {REGISTER_OUT, (uint8_t)(level ? state[1] | mask : state[1] & ~mask)};
  err = i2c_master_transmit(dev_handle, set_output_cmd, 2, 1000);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "IO expander write failed: %s", esp_err_to_name(err));
  }
}

uint8_t out_signals_get_level(enum out_signals_t slot) {
  // Read current state so we can extract the desired pin.
  uint8_t state[] = {REGISTER_IN, 0x0};
  esp_err_t err = i2c_master_transmit_receive(dev_handle, state, 1, state + 1, 1, 1000);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "IO expander read failed: %s", esp_err_to_name(err));
    return 0;
  }

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
  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = I2C_IO_EXPANDER_ADDRESS,
      .scl_speed_hz = 100000,
      .scl_wait_us = 0,
      .flags = {0},
  };

  i2c_master_bus_handle_t bus_handle = hw_specs_get_i2c_handle();
  ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));
  ESP_LOGI(TAG, "Added IO Expander device to I2C bus");

  // Configure pins as output, all of them. Log-and-continue on a transient bus
  // error (e.g. warm-reboot recovery) — the I/O scan re-drives outputs every 20ms.
  uint8_t init_output_cmd[] = {REGISTER_CFG, 0x0};
  esp_err_t err = i2c_master_transmit(dev_handle, init_output_cmd, 2, 1000);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "IO expander config failed: %s", esp_err_to_name(err));
  }

  // Force all pins to low level
  uint8_t set_output_cmd[] = {REGISTER_OUT, 0x0};
  err = i2c_master_transmit(dev_handle, set_output_cmd, 2, 1000);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "IO expander clear failed: %s", esp_err_to_name(err));
  }
  ESP_LOGI(TAG, "IO Expander initialised");
}
