#include "display.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/rtc_io.h>
#include <thing_info.h>

#include "control/controller.h"

#include "events.h"
#include "state.h"

extern "C" {
#include "u8g2_esp32_hal.h"
}

#define PIN_SDA GPIO_NUM_4
#define PIN_SCL GPIO_NUM_15
#define PIN_RST GPIO_NUM_16

#define TEMPERATURE_PANEL_WIDTH 112
#define PROBE_CELL_WIDTH (TEMPERATURE_PANEL_WIDTH / 2)

const static char* TAG = "oled";
static bool g_go = true;


static void display_draw_wifi(u8g2_t *u8g2, int yPos) {
    int radius = (128 - TEMPERATURE_PANEL_WIDTH) - 6;
    u8g2_DrawCircle(u8g2, TEMPERATURE_PANEL_WIDTH + 4, yPos, 1, U8G2_DRAW_UPPER_RIGHT);
    u8g2_DrawCircle(u8g2, TEMPERATURE_PANEL_WIDTH + 4, yPos, radius * 0.4, U8G2_DRAW_UPPER_RIGHT);
    u8g2_DrawCircle(u8g2, TEMPERATURE_PANEL_WIDTH + 4, yPos, radius * 0.75, U8G2_DRAW_UPPER_RIGHT);
    u8g2_DrawCircle(u8g2, TEMPERATURE_PANEL_WIDTH + 4, yPos, radius, U8G2_DRAW_UPPER_RIGHT);
}


static void display_draw_info(u8g2_t &u8g2) {
    u8g2_ClearBuffer(&u8g2);

    u8g2_SetFont(&u8g2, u8g2_font_fur14_tr);
    u8g2_DrawStr(&u8g2, 10, 16, "Rebel Opener");

    u8g2_SetFont(&u8g2, u8g2_font_courB12_tf);
    u8g2_DrawStr(&u8g2, 25, 36, "v" FIRMWARE_VERSION);

    u8g2_SetFont(&u8g2, u8g2_font_courB10_tf);
    u8g2_DrawStr(&u8g2, 10, 56, thing_info_id());

    u8g2_SendBuffer(&u8g2);
}

static void display_draw_panel(u8g2_t &u8g2, bool drawWifi, int delay) {
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);

    while (g_go) {
        u8g2_ClearBuffer(&u8g2);

        // Draw temps, flash them when lid is open
        int y = 14;

        y+=6;
        int yPosWifi = 14;

        EventBits_t uxBits = xEventGroupWaitBits(
                status_event_group, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT, false, true, 0);

        if (!(WIFI_CONNECTED_BIT & uxBits) && !(MQTT_CONNECTED_BIT & uxBits)) {
            drawWifi = !drawWifi;
            delay = 500;
        } else if ((WIFI_CONNECTED_BIT & uxBits) && !(MQTT_CONNECTED_BIT & uxBits)) {
            drawWifi = !drawWifi;
            delay = 200;
        } else {
            drawWifi = true;
            // Then no need to go crazy, it's event triggered when changes are detected
            delay = 1000;
        }

        // WiFi symbol
        if (drawWifi) {
            display_draw_wifi(&u8g2, yPosWifi);
        }

        int x = 5;

        // Display time
        y = 12;
        u8g2_SetFont(&u8g2, u8g2_font_fur11_tr);
        time_t now;
        time(&now);
        char strftime_buf[80];
        strftime(strftime_buf, sizeof(strftime_buf), "%x", localtime(&now));
        u8g2_DrawStr(&u8g2, x, y, strftime_buf);

        y = 34;
        u8g2_SetFont(&u8g2, u8g2_font_fur14_tr);
        strftime(strftime_buf, sizeof(strftime_buf), "%H:%M:%S", localtime(&now));
        u8g2_DrawStr(&u8g2, x, y, strftime_buf);

        // Door state
        u8g2_SetFont(&u8g2, u8g2_font_fur14_tr);

        u8g2_SendBuffer(&u8g2);
        xEventGroupWaitBits(status_event_group, REFRESH_DISPLAY_BIT, true, true, delay / portTICK_PERIOD_MS);
    }
}



static void do_display(void* userData) {
    ESP_LOGI(TAG, "Initialising display");
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.sda   = PIN_SDA;
    u8g2_esp32_hal.scl  = PIN_SCL;
    u8g2_esp32_hal.reset = PIN_RST;

    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8g2_t u8g2;
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&u8g2.u8x8,0x78);

    u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in sleep mode after this,
    u8g2_SetPowerSave(&u8g2, 0); // wake up display
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);

    ESP_LOGI(TAG, "Display initialised");
    bool drawWifi = true;

    // First display firmware version
    display_draw_info(u8g2);
    vTaskDelay(600 / portTICK_PERIOD_MS);

    int delay = 500;
    display_draw_panel(u8g2, drawWifi, delay);

    // Kill task
    vTaskDelete(NULL);
}

void display_init() {
    xTaskCreate(do_display, "do_display", 4596, NULL, 5, NULL);
}
