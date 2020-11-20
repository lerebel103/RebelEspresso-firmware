#include "homekit.h"

#include <esp_log.h>
#include <cstring>
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
}


void homekit_init() {
    if (s_init) {
        return;
    }
    s_init = true;
    ESP_LOGI(TAG, "Home kit initialised");
}

void homekit_terminate() {

}


void homekit_tick(TickType_t tickMS) {
    if (!s_init) {
        return;
    }

   if (_state_ev_handle) {
        auto current_state = (uint8_t*) _state_read(NULL);
        if (s_state_last_sent != *current_state) {

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
