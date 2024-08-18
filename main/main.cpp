
extern "C" {
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
}

#include <esp_event.h>
#include "aws_connector.h"
#include "hw_specs.h"
#include "thing_info.h"
#include "state.h"

#define TAG  "main"

// Event group pointer, so we get system events to sync up
EventGroupHandle_t status_event_group;

extern "C" void app_main() {
  esp_log_level_set("coreMQTT", ESP_LOG_ERROR);
  esp_log_level_set("gpio", ESP_LOG_ERROR);

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  status_event_group = xEventGroupCreate();

  // Init hardware as early as possible
  hw_specs_init();
  thing_info_init();

  ESP_LOGI(TAG, "Starting AWS connector");
  aws_connector_init(status_event_group);

  // state_print_system_info();
  //controller_init();
  // Here's our control loop
  //controller_enter_loop();
  //esp_event_loop_delete_default();
}


