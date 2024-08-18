#include "hw_specs.h"

#include <src/hw/base/boiler_temp.h>
#include <src/hw/base/brew_temp.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <driver/gpio.h>

#include "src/hw/base/out_signals.h"
#include "hw_config.h"
#include "ADS124S08.h"

#define TAG "hw_specs"

static i2c_master_bus_handle_t s_i2c_handle;

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

i2c_master_bus_handle_t hw_specs_get_i2c_handle() {
  return s_i2c_handle;
}

void hw_specs_init() {
  ESP_LOGI(TAG, "Creating I2C bus");
  // I2C Initialisation
  i2c_master_bus_config_t conf = {
      .i2c_port = I2C_MASTER_NUM,
      .sda_io_num = I2C_PIN_SDA,
      .scl_io_num = I2C_PIN_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .intr_priority = 0,
      .trans_queue_depth = 0,
      .flags = {.enable_internal_pullup = true},
  };

  ESP_ERROR_CHECK(i2c_new_master_bus(&conf, &s_i2c_handle));
  ESP_LOGI(TAG, "I2C bus created");

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

void hw_specs_handle_new_temp(uint64_t time_us, const struct measure_t data, uint8_t idx) {
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
