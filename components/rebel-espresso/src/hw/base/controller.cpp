#include "controller.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <events.h>
#include <hal/timer_types.h>
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
#include "process_image.h"
#include "io_scan.h"
#include "sensor_task.h"

#define KEY_ENABLED "ctrl_enabled"

static const char *TAG = "controller";

static rtds_cfg_t s_rtds_cfg;

static controller_cfg_t g_controller_cfg;

/* Event source task related definitions */
ESP_EVENT_DEFINE_BASE(MACHINE_EVENTS);

#define ESP_INTR_FLAG_DEFAULT (ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_LEVEL3)

static spi_host_device_t s_spi = SPI2_HOST;

void _init_spi() {
  // SPI initialisation
  spi_bus_config_t busConfig = {};
  busConfig.miso_io_num = PIN_MISO;
  busConfig.mosi_io_num = PIN_MOSI;
  busConfig.sclk_io_num = PIN_SCK;
  busConfig.quadhd_io_num = -1;
  busConfig.quadwp_io_num = -1;
  busConfig.max_transfer_sz = 4096;
  busConfig.flags = SPICOMMON_BUSFLAG_MASTER;

  esp_err_t err = spi_bus_initialize(s_spi, &busConfig, SPI_DMA_CH_AUTO);

  // INVALID_STATE means the host is already in use - that's OK
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGD(TAG, "SPI bus already initialized");
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error initialising SPI bus: %s", esp_err_to_name(err));
  }
}

void controller_init() {
  // Initialise process image first — before any task starts that reads/writes it.
  // Sets all outputs OFF, all inputs inactive, all sensors faulted (safe defaults).
  process_image_init();

  // install gpio isr service
  gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

  // Are we enabled?
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE_SYS, NVS_READWRITE, &nvs_handle));
  nvs_get_u8(nvs_handle, KEY_ENABLED, (uint8_t *)&g_controller_cfg.enabled);
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
  reset_button_init((gpio_num_t)CONFIG_RESET_GPIO);

  // Start sensor and I/O scan tasks (must be after GPIO/I2C/SPI peripherals are configured)
  sensor_task_init();
  io_scan_init();
}

void controller_enter_loop() {
  // Nothing critical here, just start the iot and network stuff here
  iot_process_events();
}
