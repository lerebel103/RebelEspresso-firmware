#include "homekit.h"

#include <esp_log.h>
#include <cstring>
#include <hap.h>
#include <esp_interface.h>
#include <esp_wifi.h>
#include <events.h>
#include <control/actuate.h>

#include "thing_info.h"
#include "version.h"

const char *TAG = "HK";

#define ACCESSORY_NAME  "Rebel Opener"
#define MANUFACTURER_NAME   "LeRebel"
#define MODEL_NAME  "RebelOpener r" HARDWARE_REVISION
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

static void* s_acc;
static bool s_init = false;
static hap_accessory_callback_t callback;

static bool s_target_needs_reset = true;
static uint8_t s_current_state_last_sent = 254;
static uint8_t s_obstruction_last_sent = 254;
static uint8_t s_target_state_last_sent = 254;

static uint8_t s_target_state = 1;

static void *_current_state_ev_handle;
static void *_target_state_ev_handle;
static void *_obstruction_ev_handle;


void* identify_read(void*) {
    return nullptr;
}

void *_current_state_read(void *arg) {
    LWIP_UNUSED_ARG(arg);

    Actuate_State_t state = actuate_get_state();

    static uint8_t val = 0;
    if (state == ACTUATE_STATE_OPENED) {
        val = 0;
    } else if (state == ACTUATE_STATE_CLOSED) {
        val = 1;
    } else if (state == ACTUATE_STATE_OPENING) {
        val = 2;
    } else if (state == ACTUATE_STATE_CLOSING) {
        val = 3;
    } else if (state == ACTUATE_STATE_STOPPED) {
        val = 4;
    }

    return (void *) &val;
}

void _current_state_notify(void *arg, void *ev_handle, bool enable) {
    LWIP_UNUSED_ARG(arg);

    if (enable) {
        _current_state_ev_handle = ev_handle;
    } else {
        _current_state_ev_handle = nullptr;
    }
}


void *_target_state_read(void *arg) {
    LWIP_UNUSED_ARG(arg);
    return (void *) &s_target_state;
}


void _target_state_write(void *arg, void *value, int len) {
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(len);
    LWIP_UNUSED_ARG(arg);

    s_target_state = *(uint8_t*)value;
    ESP_LOGW(TAG, "Got new target state %d", s_target_state);

    Actuate_State_t current = actuate_get_state();
    if (current != s_target_state) {
        if (s_target_state == 0 || s_target_state == 2) {
            // open or opening
            if (current == ACTUATE_STATE_CLOSING) {
                // Stop it was closing
                actuate_blip_switch();
                vTaskDelay(250 / portTICK_PERIOD_MS);
                // Now that it is stopped, open
                actuate_blip_switch();
            } else {
                actuate_blip_switch();
            }
        } else if (s_target_state == 1 || s_target_state == 3) {
            // close or closing
            if (current == ACTUATE_STATE_OPENING) {
                // Stop it was opening
                actuate_blip_switch();
                vTaskDelay(250 / portTICK_PERIOD_MS);
                // Now that it is stopped, close
                actuate_blip_switch();
            } else {
                actuate_blip_switch();
            }
        }
        // stopped ignored
    } else {
        ESP_LOGI(TAG, "Desired state is already reached");
    }
}

void _target_state_notify(void *arg, void *ev_handle, bool enable) {
    LWIP_UNUSED_ARG(arg);

    if (enable) {
        _target_state_ev_handle = ev_handle;
    } else {
        _target_state_ev_handle = nullptr;
    }
}

void *_obstruction_read(void *arg) {
    LWIP_UNUSED_ARG(arg);
    static bool val = (actuate_get_state() == ACTUATE_STATE_OBSTRUCTION);

    return (void *) &val;
}


void _obstruction_notify(void *arg, void *ev_handle, bool enable) {
    LWIP_UNUSED_ARG(arg);

    if (enable) {
        _obstruction_ev_handle = ev_handle;
    } else {
        _obstruction_ev_handle = nullptr;
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


    /**
     Defines that the accessory has control over the opening of a garage door.
     Required Characteristics:
     - CURRENT_DOOR_STATE
     - current_state_DOOR_STATE
     - OBSTRUCTION_DETECTED

     Optional Characteristics:
     - NAME
     - LOCK_CURRENT_STATE
     - LOCK_current_state_STATE
     */

    // Ambient temperature
    ESP_LOGI(TAG, "Adding characteristics");
    struct hap_characteristic ambient_temperature[] = {
            {
                    HAP_CHARACTER_NAME,
                    (void *) "DoorOpener",
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    NO_VALUE_SPECIFICS
            },
            {
                    HAP_CHARACTER_CURRENT_DOOR_STATE,
                    _current_state_read(nullptr),
                    nullptr,
                    _current_state_read,
                    nullptr,
                    _current_state_notify,
                    NO_VALUE_SPECIFICS

            },
            {
                    HAP_CHARACTER_TARGET_DOORSTATE,
                    _target_state_read(nullptr),
                    nullptr,
                    _target_state_read,
                    _target_state_write,
                    _target_state_notify,
                    NO_VALUE_SPECIFICS

            },
            {
                    HAP_CHARACTER_OBSTRUCTION_DETECTED,
                    _obstruction_read(nullptr),
                    nullptr,
                    _obstruction_read,
                    nullptr,
                    _obstruction_notify,
                    NO_VALUE_SPECIFICS
            },
    };
    hap_service_and_characteristics_add(s_acc, accessory_object, HAP_SERVICE_GARAGE_DOOR_OPENER, ambient_temperature,
                                        ARRAY_SIZE(ambient_temperature));

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
                                   (char *) MANUFACTURER_NAME, HAP_ACCESSORY_CATEGORY_GARAGE, 1, nullptr,
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

    // Make sure desired state is reset to follow what the steady state of the door is.
    Actuate_State_t state = actuate_get_state();
    if (state != ACTUATE_STATE_CLOSED && state != ACTUATE_STATE_OPENED) {
        s_target_needs_reset = true;
    } else if ((state == ACTUATE_STATE_CLOSED || state == ACTUATE_STATE_OPENED) && s_target_needs_reset) {
        s_target_state = *(uint8_t*)_current_state_read(NULL);
        s_target_needs_reset = false;
    }


    if (_target_state_ev_handle) {
        auto target = (uint8_t*)_target_state_read(NULL);
        if (s_target_state_last_sent != *target) {
            hap_event_response(s_acc, _target_state_ev_handle, target);
            s_target_state_last_sent = *(uint8_t*)target;
        }
    }
    
    if (_current_state_ev_handle) {
        auto current_state = (uint8_t*)_current_state_read(NULL);
        if (s_current_state_last_sent != *current_state) {
            hap_event_response(s_acc, _current_state_ev_handle, current_state);
            s_current_state_last_sent = *(uint8_t*)current_state;
        }
    }

    if (_obstruction_ev_handle) {
        auto obstruction = (bool*)_obstruction_read(NULL);
        if (s_obstruction_last_sent != *obstruction) {
            hap_event_response(s_acc, _obstruction_ev_handle, obstruction);
            s_obstruction_last_sent = *(bool*)obstruction;
        }
    }


}
