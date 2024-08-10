#include "wifi_connect.h"

#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include <nvs_flash.h>
#include <lwip/sio.h>

#include "events.h"
#include "nvram_store.h"
#include "state.h"
#include "thing_info.h"
#include "version.h"
#include "sntp.h"
#include "qrcode.h"
#include "sys_reset.h"

#define WIFI_TAG "wifi"
#define NVRAM_WIFI_TX_POWER "wifi_tx_pwr"

#define PROV_QR_VERSION         "v1"
#define PROV_TRANSPORT_BLE      "ble"
#define QRCODE_BASE_URL         "https://espressif.github.io/esp-jumpstart/qrcode.html"

static uint32_t g_wifi_error_count = 0;
static TickType_t g_wifi_connect_start;
static TickType_t g_wifi_last_connected = 0;
static TickType_t g_wifi_last_connect_attempt = 0;
static bool s_provisioning_failed = false;
static uint8_t* s_qrcode = nullptr;
static int s_qr_len = 0;

static esp_netif_t *s_netif = nullptr;


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


static void wifi_init_sta(void) {
    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void _prov_event_handler(void *ctx,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                void *event_data) {
    LWIP_UNUSED_ARG(ctx);
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(WIFI_TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                auto *wifi_sta_cfg = (wifi_sta_config_t *) event_data;
                ESP_LOGI(WIFI_TAG, "Received Wi-Fi credentials for SSID %s",
                         (const char *) wifi_sta_cfg->ssid);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                auto *reason = (wifi_prov_sta_fail_reason_t *) event_data;
                ESP_LOGE(WIFI_TAG, "Provisioning failed!\n\tReason : %s"
                                   "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");

                s_provisioning_failed = true;
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(WIFI_TAG, "Provisioning successful");
                s_provisioning_failed = false;
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                xEventGroupClearBits(status_event_group, PROVISIONING_BIT);

                break;
            case WIFI_PROV_DEINIT:
                ESP_LOGI(WIFI_TAG, "Provisioning DONE.");
                xEventGroupClearBits(status_event_group, PROVISIONING_BIT);
                break;
            default:
                break;
        }
    }
}

/**
 * We collect any wifi-related event here so we can deal with connect/disconnect events
 */
static void _wifi_ip_event_handler(void *ctx,
                                   esp_event_base_t event_base,
                                   int32_t event_id,
                                   void *event_data) {
    LWIP_UNUSED_ARG(ctx);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifi_config_t config = {};
        ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &config));
        ESP_LOGI(WIFI_TAG, "Attempting connection to SSID: '%s'", config.sta.ssid);

        g_wifi_connect_start = xTaskGetTickCount() * portTICK_PERIOD_MS;

        esp_wifi_connect();
        g_wifi_last_connect_attempt = 0;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(WIFI_TAG, " !! WiFi connected !!");
        g_wifi_last_connect_attempt = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Set host name as STA client
        char hostname[33];
        sprintf(hostname, "%s-%s", THING_TYPE, thing_info_id());
        ESP_ERROR_CHECK(esp_netif_set_hostname(s_netif, hostname));
    } else if (event_base == WIFI_EVENT &&
               (event_id == WIFI_EVENT_STA_DISCONNECTED || event_id == WIFI_EVENT_STA_AUTHMODE_CHANGE)) {

        xEventGroupClearBits(status_event_group, WIFI_CONNECTED_BIT);
        wifi_inc_error_count();

        ESP_LOGW(WIFI_TAG, "> WiFi Disconnected, attempting re-connect");
        esp_wifi_connect();

        g_wifi_last_connect_attempt = 0;
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

        // Save power
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        uint32_t power;
        nvram_store_read_u32(NVRAM_WIFI_TX_POWER, &power, 87);
        if (power > 72) {
            power = 72;
        }
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power((int) power));


        xEventGroupSetBits(status_event_group, WIFI_CONNECTED_BIT);
    } else {
        // ESP_LOGE(WIFI_TAG, "Event not handled base=%s, id=%d", event_base, event_id);
    }
}

static void _app_data_cb(void *user_data, wifi_prov_cb_event_t event, void *event_data) {

}

/* Handler for the optional provisioning endpoint registered by the application.
 * The data format can be chosen by applications. Here, we are using plain ascii text.
 * Applications can choose to use other formats like protobuf, JSON, XML, etc.
 */
static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                          uint8_t **outbuf, ssize_t *outlen, void *priv_data) {
    if (inbuf) {
        ESP_LOGI(WIFI_TAG, "Received data: %.*s", inlen, (char *) inbuf);
    }
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *) strdup(response);
    if (*outbuf == NULL) {
        ESP_LOGE(WIFI_TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}

static void print_qr(esp_qrcode_handle_t qrcode) {
    // Copy qr content
    if (s_qrcode == nullptr) {
        s_qrcode = (uint8_t *)calloc(1, s_qr_len);
    }

    memcpy(s_qrcode, qrcode, s_qr_len);
    esp_qrcode_print_console(qrcode);
}

static void wifi_prov_print_qr(const char *name, const char *pop, const char *transport) {
    if (!name || !transport) {
        ESP_LOGW(WIFI_TAG, "Cannot generate QR code payload. Data missing.");
        return;
    }
    static char payload[150] = {0};

    if (pop) {
        snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\"" \
                    ",\"pop\":\"%s\",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, pop, transport);
    } else {
        snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\"" \
                    ",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, transport);
    }
    ESP_LOGI(WIFI_TAG, "Scan this QR code from the provisioning application for Provisioning.");
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.max_qrcode_version = 10;
    cfg.display_func = print_qr;

    esp_qrcode_generate(&cfg, payload);
    ESP_LOGI(WIFI_TAG, "If QR code is not visible, copy paste the below URL in a browser.\n%s?data=%s", QRCODE_BASE_URL,
             payload);
}

const uint8_t* wifi_get_prov_qr() {
    return s_qrcode;
}

int wifi_get_prov_qr_len() {
    return esp_qrcode_get_size(s_qrcode);
}

void wifi_init() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &_wifi_ip_event_handler,
                                               nullptr));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &_wifi_ip_event_handler,
                                               nullptr));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &_prov_event_handler,
                                               nullptr));

    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Now start the provisioning manager
    wifi_prov_mgr_config_t config = {
            .scheme = wifi_prov_scheme_ble,
            .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
            .app_event_handler = {
                    .event_cb = _app_data_cb,
                    .user_data = nullptr
            }
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    /* If device is not yet provisioned start provisioning service */
    if (!provisioned) {
        ESP_LOGI(WIFI_TAG, "Starting provisioning");

        /* What is the Device Service Name that we want
         * This translates to :
         *     - Wi-Fi SSID when scheme is wifi_prov_scheme_softap
         *     - device name when scheme is wifi_prov_scheme_ble
         */
        const char *service_name = thing_info_id();

        /* What is the security level that we want (0 or 1):
         *      - WIFI_PROV_SECURITY_0 is simply plain text communication.
         *      - WIFI_PROV_SECURITY_1 is secure communication which consists of secure handshake
         *          using X25519 key exchange and proof of possession (pop) and AES-CTR
         *          for encryption/decryption of messages.
         */
        wifi_prov_security_t security = WIFI_PROV_SECURITY_1;

        /* Do we want a proof-of-possession (ignored if Security 0 is selected):
         *      - this should be a string with length > 0
         *      - NULL if not used
         */
        const char *pop = "abcd1234";

        /* What is the service key (could be NULL)
         * This translates to :
         *     - Wi-Fi password when scheme is wifi_prov_scheme_softap
         *     - simply ignored when scheme is wifi_prov_scheme_ble
         */
        const char *service_key = NULL;

        /* This step is only useful when scheme is wifi_prov_scheme_ble. This will
         * set a custom 128 bit UUID which will be included in the BLE advertisement
         * and will correspond to the primary GATT service that provides provisioning
         * endpoints as GATT characteristics. Each GATT characteristic will be
         * formed using the primary service UUID as base, with different auto assigned
         * 12th and 13th bytes (assume counting starts from 0th byte). The client side
         * applications must identify the endpoints by reading the User Characteristic
         * Description descriptor (0x2901) for each characteristic, which contains the
         * endpoint name of the characteristic */
        uint8_t custom_service_uuid[] = {
                /* LSB <---------------------------------------
                 * ---------------------------------------> MSB */
                0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
                0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
        };

        wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

        /* An optional endpoint that applications can create if they expect to
         * get some additional custom data during provisioning workflow.
         * The endpoint name can be anything of your choice.
         * This call must be made before starting the provisioning.
         */
        wifi_prov_mgr_endpoint_create("custom-data");

        /* Start provisioning service */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key));

        /* The handler for the optional endpoint created above.
         * This call must be made after starting the provisioning, and only if the endpoint
         * has already been created above.
         */
        wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);

        /* Print QR code for provisioning */
        wifi_prov_print_qr(service_name, pop, PROV_TRANSPORT_BLE);

        xEventGroupSetBits(status_event_group, PROVISIONING_BIT);
    } else {
        xEventGroupClearBits(status_event_group, PROVISIONING_BIT);
        ESP_LOGI(WIFI_TAG, "Already provisioned, starting Wi-Fi STA");

        /* We don't need the manager as device is already provisioned,
         * so let's release it's resources */
        wifi_prov_mgr_deinit();

        /* Start Wi-Fi station */
        wifi_init_sta();
    }

}


void wifi_terminate() {
    ESP_ERROR_CHECK(esp_wifi_stop());
    while (xEventGroupWaitBits(status_event_group, WIFI_CONNECTED_BIT, false, true, 100 / portTICK_PERIOD_MS)
           & WIFI_CONNECTED_BIT) {}

    free(s_qrcode);
    s_qrcode = nullptr;
}

void wifi_set_tx_power(int power) {
    nvram_store_write_u32(NVRAM_WIFI_TX_POWER, (uint32_t) power);
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(power));
}


void wifi_tick(TickType_t time_ms) {
    // Check if we are connected but aren't receiving an IP, restart WiFi.
    if (!(xEventGroupGetBits(status_event_group) & WIFI_CONNECTED_BIT) && g_wifi_last_connect_attempt > 0) {
        if (time_ms > g_wifi_last_connect_attempt && (time_ms - g_wifi_last_connect_attempt) > 10000) {
            // Nope, we are not getting an IP, start over again
            ESP_LOGW(WIFI_TAG, "I have no IP, restarting WiFi, timeout=%" PRIu32 , time_ms - g_wifi_last_connect_attempt);
            esp_wifi_disconnect();
        }
    }


    // Hack to reboot if we have no wifi for extended periods.
    if ((xEventGroupGetBits(status_event_group) & WIFI_CONNECTED_BIT)) {
        g_wifi_last_connected = g_wifi_last_connect_attempt = xTaskGetTickCount() * portTICK_PERIOD_MS;
    } else if (time_ms - g_wifi_last_connected > 60 * 30 * 1000) {
        ESP_LOGE(WIFI_TAG, "No wifi for too long, restarting");
        esp_restart();
    }

    // Then reboot, simple strategy to get provisioning going again
    if (s_provisioning_failed) {
        // Reset and restart then
        vTaskDelay(pdMS_TO_TICKS(1000));
        sys_reset_nvs_restart();
    }
}
