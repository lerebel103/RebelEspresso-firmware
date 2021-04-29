#include "thing_info.h"
#include <lwipopts.h>

#include <esp_log.h>
#include <memory.h>

#include "sys/nvram_store.h"

#define THING_ID "thing_id"

static char g_macStr[32] = {0};

const static int THING_ID_MAX = 64;
static char g_thing_id[THING_ID_MAX] = {0};

/**
 * Gives us a native identifier based on the PCB mac address (WiFi)
 */
static void set_default_thing_id() {
    uint8_t l_Mac[6];
    esp_efuse_mac_get_default(l_Mac);

    // Convert MAC address as a unique ID. It must start with a letter and contain no special characters
    // so it can work with GCP
    snprintf(g_macStr, sizeof(g_macStr), "m%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
             l_Mac[0], l_Mac[1], l_Mac[2], l_Mac[3], l_Mac[4], l_Mac[5]);
    strlwr(g_macStr);
}

const char *thing_info_id() {
    if (strlen(g_thing_id) == 0) {
        return g_macStr;
    } else {
        return g_thing_id;
    }
}

void thing_info_init() {
    set_default_thing_id();
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE_SYS, NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvram_store_get_str(nvs_handle, THING_ID, (char *) g_thing_id, THING_ID_MAX, ""));
    nvs_close(nvs_handle);
}

