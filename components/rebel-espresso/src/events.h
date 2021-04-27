#pragma once

#include <freertos/event_groups.h>
#include <esp_event_base.h>


extern EventGroupHandle_t status_event_group;

#define POWER_ON_BIT                BIT0
#define BOILER_LEVEL_OK_BIT         BIT1
#define WIFI_CONNECTED_BIT          BIT2
#define TIME_SYNC_BIT               BIT3
#define MQTT_CONNECTED_BIT          BIT4
#define OTA_PERFORMED_BIT           BIT5
#define REFRESH_DISPLAY_BIT         BIT6
#define SEND_STATE_BIT              BIT7
#define DESCALE_MODE_BIT            BIT8
#define PROVISIONING_BIT            BIT9



ESP_EVENT_DECLARE_BASE(BUTTONS_EVENTS);

enum button_events_t {
    BUTTON_CONTROLLER_PRESSED,
    BUTTON_CONTROLLER_HELD,
    BUTTON_UP_PRESSED,
    BUTTON_DOWN_PRESSED,
};

ESP_EVENT_DECLARE_BASE(MACHINE_EVENTS);

enum machine_events_t {
    POWER_STANDBY,
    POWER_ACTIVE,
    TICK,
    BREW_STARTED,
    BREW_STOPPED,
    BOILER_REFILL_STARTED,
    BOILER_REFILL_STOPPED,
    BOILER_REFILL_ERROR
};