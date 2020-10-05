#pragma once

#include <freertos/event_groups.h>
#include <esp_event_base.h>


extern EventGroupHandle_t status_event_group;

#define WIFI_CONNECTED_BIT          BIT0
#define TIME_SYNC_BIT               BIT1
#define MQTT_CONNECTED_BIT          BIT2
#define OTA_PERFORMED_BIT           BIT3
#define LED_DONE_BIT                BIT4
#define REFRESH_DISPLAY_BIT         BIT5
#define SEND_STATE_BIT              BIT6



ESP_EVENT_DECLARE_BASE(TOUCH_BUTTONS_EVENTS);

enum touch_button_events_t {
    BUTTON_CONTROLLER_PRESSED,
    BUTTON_CONTROLLER_HELD,
    BUTTON_UP_PRESSED,
    BUTTON_DOWN_PRESSED,
};