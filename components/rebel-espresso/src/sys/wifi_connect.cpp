#include "wifi_connect.h"

#include <string.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>

#include "events.h"
#include "nvram_store.h"
#include "state.h"
#include "thing_info.h"
#include "version.h"
#include "sntp.h"

#define WIFI_TAG "wifi"
#define NVRAM_WIFI_SSID "wifi_ssid"
#define NVRAM_WIFI_PASSWORD "wifi_password"
#define NVRAM_WIFI_TX_POWER "wifi_tx_pwr"

static wifi_config_t wifi_config;
static uint32_t g_wifi_error_count = 0;
static TickType_t g_wifi_connect_start;
static TickType_t g_wifi_last_connected = 0;

static esp_netif_t* s_netif = nullptr;


void wifi_inc_error_count() {
    uint32_t count = wifi_get_error_count() + 1;
    esp_err_t err = nvram_store_write_u32("wifi_error_cnt", count);
    ESP_ERROR_CHECK(err);
    g_wifi_error_count = count;
}

uint32_t wifi_get_error_count() {
    if (g_wifi_error_count == 0) {
        // This is our default value
        uint32_t val;
        esp_err_t err = nvram_store_read_u32("wifi_error_cnt", &val, 0);
        ESP_ERROR_CHECK(err);
        g_wifi_error_count = val;
    }
    return g_wifi_error_count;
}


/**
 * We collect any wifi-related event here so we can deal with connect/disconnect events
 */
void event_handler(void *ctx,
                   esp_event_base_t event_base,
                   int32_t event_id,
                   void *event_data) {
    LWIP_UNUSED_ARG(ctx);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(WIFI_TAG, "WiFi Attempting connection");
        g_wifi_connect_start = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (strlen(reinterpret_cast<const char *>(wifi_config.sta.ssid)) > 0) {
            esp_wifi_connect();
        }
        g_wifi_last_connected = 0;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(WIFI_TAG, " !! WiFi connected !!");
        g_wifi_last_connected = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Set host name as STA client
        char hostname[33];
        sprintf(hostname, "%s-%s", THING_TYPE, thing_info_id());
        ESP_ERROR_CHECK(esp_netif_set_hostname(s_netif, hostname));
    } else if (event_base == WIFI_EVENT &&
              ( event_id == WIFI_EVENT_STA_DISCONNECTED || event_id == WIFI_EVENT_STA_AUTHMODE_CHANGE) ) {

        xEventGroupClearBits(status_event_group, WIFI_CONNECTED_BIT);
        if (strlen((const char *) wifi_config.sta.ssid) > 0) {
            wifi_inc_error_count();

            ESP_LOGI(WIFI_TAG, "> WiFi Disconnected, attempting re-connect");
            esp_wifi_connect();
        }

        g_wifi_last_connected = 0;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto event = static_cast<ip_event_got_ip_t *>(event_data);

        // Yippee - Wifi Connected
        wifi_ap_record_t ap_info;
        esp_wifi_sta_get_ap_info(&ap_info);
        state_get().wifi_rssi = ap_info.rssi;
        snprintf(state_get().wifi_bssid, sizeof(state_get().wifi_bssid),
                 "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX",
                 ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2], ap_info.bssid[3], ap_info.bssid[4],
                 ap_info.bssid[5]);
        state_get().wifi_primary_channel = ap_info.primary;
        state_get().wifi_join_duration = xTaskGetTickCount() * portTICK_PERIOD_MS - g_wifi_connect_start;

        char gw_addr[16];

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
        char ip[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip, 16);

        esp_ip4addr_ntoa(&event->ip_info.gw, gw_addr, 16);
        ESP_LOGI(WIFI_TAG, "WiFi Connected, IP: %s RSSI: %d", ip, ap_info.rssi);
#else
        strcpy(gw_addr, ip4addr_ntoa(&event->event_info.got_ip.ip_info.gw));
        ESP_LOGI(WIFI_TAG, "WiFi Connected, IP: %s RSSI: %d", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip), ap_info.rssi);
#endif

        xEventGroupSetBits(status_event_group, WIFI_CONNECTED_BIT);

        // Good, start SNTP then
        sntp_sync_init(gw_addr);
    } else {
        ESP_LOGD(WIFI_TAG, "Event not handled base=%s, id=%d", event_base, event_id);
    }
}

void wifi_init() {
    ESP_ERROR_CHECK(nvram_store_read_str(
            NVRAM_WIFI_SSID, (char *) &wifi_config.sta.ssid, 32, ""));
    ESP_ERROR_CHECK(nvram_store_read_str(
            NVRAM_WIFI_PASSWORD, (char *) &wifi_config.sta.password, 64, ""));

    ESP_LOGI(WIFI_TAG, "Joining wifi ssid=%s", wifi_config.sta.ssid);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &event_handler,
                                               nullptr));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &event_handler,
                                               nullptr));

    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT()
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Save power
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);

    uint32_t power;
    nvram_store_read_u32(NVRAM_WIFI_TX_POWER, &power, 87);
    if (power > 72) {
        power = 72;
    }
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power((int) power));
}


void wifi_terminate() {
    wifi_config.sta.ssid[0] = '\0';
    ESP_ERROR_CHECK(esp_wifi_stop());
    while (xEventGroupWaitBits(status_event_group, WIFI_CONNECTED_BIT, false, true, 100 / portTICK_PERIOD_MS)
           & WIFI_CONNECTED_BIT) {}
}


void wifi_set_ssid(const char *ssid) {
    // Stop the thing, set new ssid and restart WiFi
    esp_wifi_stop();

    strncpy((char *) wifi_config.sta.ssid, ssid, strlen(ssid));
    wifi_config.sta.ssid[strlen(ssid)] = '\0';
    nvram_store_write_str(NVRAM_WIFI_SSID, (const char *) wifi_config.sta.ssid);

    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_set_mode(WIFI_MODE_STA);
}


void wifi_set_password(const char *password) {
    // Stop the thing, set new password and restart WiFi
    esp_wifi_stop();

    strncpy((char *) wifi_config.sta.password, password, strlen(password));
    wifi_config.sta.password[strlen(password)] = '\0';
    nvram_store_write_str(NVRAM_WIFI_PASSWORD, (const char *) wifi_config.sta.password);

    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_set_mode(WIFI_MODE_STA);
}

void wifi_set_tx_power(int power) {
    nvram_store_write_u32(NVRAM_WIFI_TX_POWER, (uint32_t) power);
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(power));
}


void wifi_tick(TickType_t time_ms) {
    // Check if we are connected but aren't receiving an IP, restart WiFi.
    if (! (xEventGroupGetBits(status_event_group) & WIFI_CONNECTED_BIT) && g_wifi_last_connected > 0) {
        if (time_ms > g_wifi_last_connected && (time_ms - g_wifi_last_connected) > 10000) {
            // Nope, we are not getting an IP, start over again
            ESP_LOGW(WIFI_TAG, "I have no IP, restarting WiFi, timeout=%d", time_ms - g_wifi_last_connected);
            esp_wifi_disconnect();
        }
    }
}
