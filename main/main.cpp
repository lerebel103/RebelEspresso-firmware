
extern "C" {
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
}

#include <esp_event.h>
#include <src/hw/base/hw_specs.h>
#include <driver/gpio.h>
#include "state.h"
#include "controller.h"
#include "thing_info.h"
#include "sys/nvram_store.h"

#define TAG  "main"

// Event group pointer so we get system events to sync up
EventGroupHandle_t status_event_group;

extern "C" void app_main() {
  esp_log_level_set("coreMQTT", ESP_LOG_ERROR);

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  gpio_install_isr_service(0);
  status_event_group = xEventGroupCreate();

  // Init hardware as early as possible
  nvram_store_init();
  store_inc_cycle_count();
  hw_specs_init();
  thing_info_init();

  esp_log_level_set("gpio", ESP_LOG_ERROR);

  state_print_system_info();
  controller_init();

  // Here's our control loop
  controller_enter_loop();
  esp_event_loop_delete_default();
}


