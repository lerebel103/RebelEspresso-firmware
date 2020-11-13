#include "homekit.h"

#include <esp_log.h>
#include <cstring>
#include <hap.h>
#include <esp_interface.h>
#include <esp_wifi.h>
#include <events.h>
#include <src/control/power.h>

#include "thing_info.h"
#include "version.h"

const char *TAG = "HK";

#define ACCESSORY_NAME  "RebelEspresso"
#define MANUFACTURER_NAME   "LeRebel"
#define MODEL_NAME THING_TYPE " r" HARDWARE_REVISION
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

static void* s_acc;
static bool s_init = false;
static hap_accessory_callback_t callback;

static uint8_t s_state_last_sent = 254;
static uint8_t s_fault_last_sent = 254;

static void *_state_ev_handle;
static void *_fault_ev_handle;


void* identify_read(void*) {
    return nullptr;
}

static void *_state_read(void *arg) {
    LWIP_UNUSED_ARG(arg);
    static int val = 0;
    if (power_is_active()) {
        val = 1;
    } else {
        val = 0;
    }

    return (void *) &val;
}

static void _state_write(void *arg, void *value, int len) {
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(len);
    LWIP_UNUSED_ARG(arg);

    bool on = *(uint8_t*)value;
    if (on) {
        ESP_LOGW(TAG, "Got new target state ON");
        power_active();
    } else {
        ESP_LOGW(TAG, "Got new target state OFF");
        power_standby();
    }
}

static void _state_notify(void *arg, void *ev_handle, bool enable) {
    LWIP_UNUSED_ARG(arg);

    if (enable) {
        _state_ev_handle = ev_handle;
    } else {
        _state_ev_handle = nullptr;
    }
}

static void *_fault_read(void *arg) {
    LWIP_UNUSED_ARG(arg);
    static int val = 0;
    return (void *) &val;
}

static void _fault_notify(void *arg, void *ev_handle, bool enable) {
    LWIP_UNUSED_ARG(arg);

    if (enable) {
        _fault_ev_handle = ev_handle;
    } else {
        _fault_ev_handle = nullptr;
    }
}


void hap_object_init(void *arg) {
    void *accessory_object = hap_accessory_add(s_acc);
    struct hap_characteristic cs[] = {
            {HAP_CHARACTER_IDENTIFY,
                    (void *) true,              nullptr, identify_read, nullptr, nullptr, NO_VALUE_SPECIFICS},
            {HAP_CHARACTER_MANUFACTURER,
                    (void *) MANUFACTURER_NAME, nullptr, nullptr,       nullptr, nullptr, NO_VALUE_SPECIFICS},
            {HAP_CHARACTER_MODEL,
                    (void *) MODEL_NAME,        nullptr, nullptr,       nullptr, nullptr, NO_VALUE_SPECIFICS},
            {HAP_CHARACTER_NAME,
                    (void *) ACCESSORY_NAME,    nullptr, nullptr,       nullptr, nullptr, NO_VALUE_SPECIFICS},
            {HAP_CHARACTER_SERIAL_NUMBER,
                    (void *) thing_info_id(),   nullptr, nullptr,       nullptr, nullptr, NO_VALUE_SPECIFICS},
            {HAP_CHARACTER_FIRMWARE_REVISION,
                    (void *) FIRMWARE_VERSION,  nullptr, nullptr,       nullptr, nullptr, NO_VALUE_SPECIFICS},
    };
    hap_service_and_characteristics_add(s_acc, accessory_object, HAP_SERVICE_ACCESSORY_INFORMATION, cs, ARRAY_SIZE(cs));

    ESP_LOGI(TAG, "Adding characteristics");
    struct hap_characteristic switches[] = {
            {
                    HAP_CHARACTER_ON,
                    _state_read(nullptr),
                    nullptr,
                    _state_read,
                    _state_write,
                    _state_notify,
                    NO_VALUE_SPECIFICS

            }/*,
            {
                    HAP_CHARACTER_STATUS_FAULT,
                    _fault_read(nullptr),
                    nullptr,
                    _fault_read,
                    nullptr,
                    _fault_notify,
                    NO_VALUE_SPECIFICS

            }*/
    };
    hap_service_and_characteristics_add(s_acc, accessory_object, HAP_SERVICE_SWITCHS, switches,
                                        ARRAY_SIZE(switches));

}


void homekit_init() {
    if (s_init) {
        return;
    }

    hap_init(811);

    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    char accessory_id[32] = {0,};
    sprintf(accessory_id, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    callback.hap_object_init = hap_object_init;
    ESP_LOGI(TAG, "Home kit initialising with accessory_id %s", accessory_id);
    s_acc = hap_accessory_register((char *) ACCESSORY_NAME, accessory_id, (char *) "111-23-456",
                                   (char *) MANUFACTURER_NAME, HAP_ACCESSORY_CATEGORY_SWITCH, 1, nullptr,
                                   &callback);

    s_init = true;
    ESP_LOGI(TAG, "Home kit initialised");
}

void homekit_terminate() {
    hap_terminate();
}


void homekit_tick(TickType_t tickMS) {
    if (!s_init) {
        return;
    }

   if (_state_ev_handle) {
        auto current_state = (uint8_t*) _state_read(NULL);
        if (s_state_last_sent != *current_state) {
            hap_event_response(s_acc, _state_ev_handle, current_state);
            s_state_last_sent = *(uint8_t*)current_state;
        }
    }

    /*if (_fault_ev_handle) {
        auto fault = (bool*)_fault_read(NULL);
        if (s_fault_last_sent != *fault) {
            hap_event_response(s_acc, _fault_ev_handle, fault);
            s_fault_last_sent = *(bool*)fault;
        }
    }*/


}
