#include "controller.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <events.h>
#include <hal/timer_types.h>
#include <driver/timer.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <cJSON.h>
#include <driver/spi_common.h>
#include <esp_timer.h>

#include "rtds.h"
#include "process_loop.h"
#include "boiler_refill.h"
#include "boiler_temp.h"
#include "hw_specs.h"
#include "brew.h"
#include "power.h"
#include "setpoint_selector.h"
#include "iot.h"
#include "brew_temp.h"
#include "schedules.h"
#include "display.h"
#include "reset_button.h"
#include "state.h"


#define KEY_ENABLED "ctrl_enabled"

static const char *TAG = "controller";

static rtds_cfg_t s_rtds_cfg;

static controller_cfg_t g_controller_cfg;

static bool _go = true;

/* Event source task related definitions */
ESP_EVENT_DEFINE_BASE(MACHINE_EVENTS);

#define ESP_INTR_FLAG_DEFAULT \
    (ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_LEVEL2 |ESP_INTR_FLAG_LEVEL3)

static spi_host_device_t s_spi = HSPI_HOST;

void _init_spi() {
  // SPI initialisation
  spi_bus_config_t busConfig = {};
  busConfig.miso_io_num = PIN_MISO;
  busConfig.mosi_io_num = PIN_MOSI;
  busConfig.sclk_io_num = PIN_SCK;
  busConfig.quadhd_io_num = -1;
  busConfig.quadwp_io_num = -1;
  busConfig.max_transfer_sz = 8192;
  busConfig.flags = SPICOMMON_BUSFLAG_MASTER;

  esp_err_t err = spi_bus_initialize(s_spi, &busConfig, 1);

  // INVALID_STATE means the host is already in use - that's OK
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGD(TAG, "SPI bus already initialized");
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error initialising SPI bus: %s", esp_err_to_name(err));
  }
}


void controller_init() {
  //install gpio isr service
  gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

  reset_button_init((gpio_num_t) CONFIG_RESET_GPIO);
  state_init();

  // Are we enabled?
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE_SYS, NVS_READWRITE, &nvs_handle));
  nvs_get_u8(nvs_handle, KEY_ENABLED, (uint8_t *) &g_controller_cfg.enabled);
  nvs_close(nvs_handle);

  // Hardware-specific implementation
  _init_spi();

  // Common hw initialisation
  display_init();
  boiler_refill_init();
  brew_temp_init();
  boiler_temp_init();
  brew_init();
  setpoint_selector_init();
  rtds_init(s_spi, &s_rtds_cfg);
  process_loop_init();
  power_init();
  schedules_init();
  iot_init();

  // Causes initial state to be sent
  xEventGroupSetBits(status_event_group, SEND_STATE_BIT);
}

void controller_enter_loop() {
  const static auto loop_interval_us = 100e3;
  while (_go) {
    // Keeps going regardless of power state, emit event forever
    auto now_us = esp_timer_get_time();
    ESP_ERROR_CHECK(esp_event_post(MACHINE_EVENTS, TICK, (void *) &now_us, sizeof(uint64_t), portMAX_DELAY));
    auto after_us = esp_timer_get_time();

    auto delay_ms = (loop_interval_us - (double) (after_us - now_us)) / 1e3;
    if (delay_ms > 0) {
      vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
  }

  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------------------------------------------------
// Config stuff
// ---------------------------------------------------------------------------------------------------------------------

void controller_cfg_to_json(cJSON *root, const char *base_key) {

  auto boiler_cfg = boiler_temp_get_cfg();
  boiler_cfg.to_json(root, base_key);

  auto BREW_TEMP_cfg = brew_temp_get_cfg();
  BREW_TEMP_cfg.to_json(root, base_key);

  auto boiler_refill_cfg = boiler_refill_get_cfg();
  boiler_refill_cfg.to_json(root, base_key);

  auto schedules_cfg = schedules_get_cfg();
  schedules_cfg.to_json(root, base_key);

  // Hardware-specific implementation
  hw_specs_cfg_to_json(root, base_key);
}

void controller_status_to_json(cJSON *root, const char *base_key) {
  auto boiler_status = boiler_temp_get_status();
  boiler_status.to_json(root, base_key);

  auto brew_temp_status = brew_temp_get_status();
  brew_temp_status.to_json(root, base_key);

  auto refill_status = boiler_refill_get_status();
  refill_status.to_json(root, base_key);

  auto schedules_status = schedules_get_status();
  schedules_status.to_json(root, base_key);

  // Hardware-specific implementation
  hw_specs_status_to_json(root, base_key);
}

void controller_handle_new_cfg(const cJSON *cfg) {
  // This is a huge memory ask
  // char* json = cJSON_Print(cfg);
  // ESP_LOGI(TAG, "Got new remote config %s", json);
  // cJSON_free(json);

  // Pass down to each component, they will deal with it - it's a bit lazy really
  boiler_temp_update_cfg(cfg);
  brew_temp_update_cfg(cfg);
  boiler_refill_update_cfg(cfg);
  schedules_update_cfg(cfg);

  // Hardware-specific implementation
  hw_specs_handle_new_cfg(cfg);

  // Trigger status send
  xEventGroupSetBits(status_event_group, SEND_STATE_BIT);
}


