#include "sys_reset.h"

#include <esp_err.h>
#include <nvs_flash.h>
#include <esp_system.h>

void sys_reset_nvs() {
    // Erase nvs partition and restart
    ESP_ERROR_CHECK(nvs_flash_erase_partition(CONFIG_HAP_PLATFORM_DEF_NVS_RUNTIME_PARTITION));
}

void sys_reset_nvs_restart() {
    sys_reset_nvs();
    esp_restart();
}
