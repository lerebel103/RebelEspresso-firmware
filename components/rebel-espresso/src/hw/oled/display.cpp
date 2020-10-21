#include "display.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/rtc_io.h>
#include <thing_info.h>
#include <hw_config.h>
#include <src/hw/rtds.h>
#include <src/control/boiler_temp.h>
#include <cmath>

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


static float temperature_to_unit(double celcius, units_enum_t unit) {
    if (unit == UNIT_FARENHEIGHT) {
        celcius = celcius * 9 / 5.0f + 32;
    }

    // Round to 1 digit only
    return (int)(round (10 * celcius)) / 10.0f;
}

static void display_draw_frame(u8g2_t *u8g2, int xSeparator, int ySeparator) {
    // H Line just below pit temp
    u8g2_DrawLine(u8g2, 0, ySeparator, xSeparator, ySeparator);

    // Right most separator
    u8g2_DrawLine(u8g2, xSeparator, 0, xSeparator, 64);

    // Vertical separator for probes
    //u8g2_DrawLine(u8g2, TEMPERATURE_PANEL_WIDTH / 2, ySeparator, TEMPERATURE_PANEL_WIDTH / 2, 64);

    // Horizontal spliter for probes / fan
    //u8g2_DrawLine(u8g2, 0, ySeparator + 23, xSeparator, ySeparator + 23);
}

static void display_draw_on_off(u8g2_t *u8g2, int yPos, bool is_on) {
    int radius = ((128 - TEMPERATURE_PANEL_WIDTH) - 5) / 2;
    u8g2_DrawDisc(u8g2, TEMPERATURE_PANEL_WIDTH + radius / 2 + 6, yPos, 1, 0);
    if (is_on) {
        u8g2_DrawDisc(u8g2, TEMPERATURE_PANEL_WIDTH + radius / 2 + 7, yPos, radius * .4,
                      U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
    }

    u8g2_DrawCircle(u8g2, TEMPERATURE_PANEL_WIDTH + radius / 2 + 7, yPos, radius,
                    U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
}

static void display_draw_wifi(u8g2_t *u8g2, int yPos) {
    int radius = (128 - TEMPERATURE_PANEL_WIDTH) - 6;
    u8g2_DrawCircle(u8g2, TEMPERATURE_PANEL_WIDTH + 4, yPos, 1, U8G2_DRAW_UPPER_RIGHT);
    u8g2_DrawCircle(u8g2, TEMPERATURE_PANEL_WIDTH + 4, yPos, radius * 0.4, U8G2_DRAW_UPPER_RIGHT);
    u8g2_DrawCircle(u8g2, TEMPERATURE_PANEL_WIDTH + 4, yPos, radius * 0.75, U8G2_DRAW_UPPER_RIGHT);
    u8g2_DrawCircle(u8g2, TEMPERATURE_PANEL_WIDTH + 4, yPos, radius, U8G2_DRAW_UPPER_RIGHT);
}

static void display_draw_unit(u8g2_t *u8g2, int yPos) {
    u8g2_SetFont(u8g2, u8g2_font_courR10_tf);
    u8g2_DrawCircle(u8g2, TEMPERATURE_PANEL_WIDTH + 6, yPos - 8, 2,
                    U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);

    if (rtds_get_unit() == UNIT_CELCIUS) {
        u8g2_DrawStr(u8g2, TEMPERATURE_PANEL_WIDTH +  8, yPos + 4, "C");
    } else {
        u8g2_DrawStr(u8g2, TEMPERATURE_PANEL_WIDTH +  8, yPos + 4, "F");
    }
}

void display_draw_pit_temp(u8g2_t *u8g2, int *y) {
    rtd_data_t result;
    rtds_get(&result, 0);

    char tempBuf[16];
    char setPointBuf[7];
    auto temp_val = result.temperature;

    boiler_temp_cfg_t cfg = boiler_temp_get_cfg();

    sprintf(setPointBuf, "/%d", (int) temperature_to_unit(cfg.pid.setpoints[cfg.pid.active_setpoint], rtds_get_unit()));

    int width_of_set_point = 10 * strlen(setPointBuf);

    // Integral part of temperature, in larger font
    if (result.fault == Max31865Error::NoError) {
        sprintf(tempBuf, "%d", (int) temp_val);
    } else {
        sprintf(tempBuf, "---");
    }


    auto width_of_intregral_temp = strlen(tempBuf) * 10;
    auto width_of_floating_point = 2 * 8;

    // Calculate offset from LHS edge
    auto x_offset = (TEMPERATURE_PANEL_WIDTH
                     - width_of_intregral_temp
                     - width_of_set_point
                     - width_of_floating_point) / 2;
    u8g2_SetFont(u8g2, u8g2_font_courB14_tf);
    u8g2_DrawStr(u8g2, x_offset, *y, tempBuf);

    // Draw floating point now, as '.x'
    int point = static_cast<int>(temp_val * 10 - static_cast<int>(temp_val) * 10);
    sprintf(tempBuf, ".%d", point);
    u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
    u8g2_DrawStr(u8g2, x_offset + width_of_intregral_temp, *y, tempBuf);

    // And now Setpoint
    auto x_setpoint = x_offset + width_of_intregral_temp + width_of_floating_point;
    u8g2_SetFont(u8g2, u8g2_font_courR10_tf);
    u8g2_DrawStr(u8g2, x_setpoint, (*y), setPointBuf);

    // Duty
    double duty = boiler_temp_get_duty();
    sprintf(tempBuf, "Duty: %d%%", (int)duty);
    auto width_of_duty_temp = strlen(tempBuf) * 10;
    u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
    u8g2_DrawStr(u8g2, 10 + (TEMPERATURE_PANEL_WIDTH - width_of_duty_temp) / 2, *y + 26, tempBuf);

}


static void display_draw_info(u8g2_t &u8g2) {
    u8g2_ClearBuffer(&u8g2);

    u8g2_SetFont(&u8g2, u8g2_font_fur14_tr);
    u8g2_DrawStr(&u8g2, 10, 16, "Rebel Espresso");

    u8g2_SetFont(&u8g2, u8g2_font_courB12_tf);
    u8g2_DrawStr(&u8g2, 25, 36, "v" FIRMWARE_VERSION);

    u8g2_SetFont(&u8g2, u8g2_font_courB10_tf);
    u8g2_DrawStr(&u8g2, 10, 56, thing_info_id());

    u8g2_SendBuffer(&u8g2);
}

static void display_draw_panel(u8g2_t &u8g2, bool drawWifi, int delay) {
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);

    bool toggle = true;
    while (g_go) {
        u8g2_ClearBuffer(&u8g2);

        // Draw temps, flash them when lid is open
        int y = 14;
        display_draw_pit_temp(&u8g2, &y);

        y+=6;
        int yPitBottom = y;

        int yPosWifi = 14;
        int yPosOnOff = 54;

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
            delay = 5000;
        }

        // Now draw frame separator
        display_draw_frame(&u8g2, TEMPERATURE_PANEL_WIDTH, yPitBottom);

        // WiFi symbol
        if (drawWifi) {
            display_draw_wifi(&u8g2, yPosWifi);
        }

        // unit
        display_draw_unit(&u8g2, yPosWifi + (yPosOnOff - yPosWifi) / 2);

        // On / Off
        toggle = !toggle;
        display_draw_on_off(&u8g2, yPosOnOff, toggle);

        u8g2_SendBuffer(&u8g2);
        xEventGroupWaitBits(status_event_group, REFRESH_DISPLAY_BIT, true, true, delay / portTICK_PERIOD_MS);
    }
}



static void do_display(void* userData) {
    ESP_LOGI(TAG, "Initialising display");

    u8g2_esp32_hal_t u8g2_esp32_hal = {};
    u8g2_esp32_hal.sda   = GPIO_NUM_NC;
    u8g2_esp32_hal.scl  = GPIO_NUM_NC;
    u8g2_esp32_hal.mosi = GPIO_MOSI;
    u8g2_esp32_hal.miso = GPIO_MISO;
    u8g2_esp32_hal.clk = GPIO_SCK;
    u8g2_esp32_hal.cs = GPIO_OLED_CS;
    u8g2_esp32_hal.reset = GPIO_NUM_NC;
    u8g2_esp32_hal.dc = GPIO_OLED_DC;

    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8g2_t u8g2;
    u8g2_Setup_ssd1322_nhd_256x64_f(&u8g2, U8G2_R0, u8g2_esp32_spi_byte_cb, u8g2_esp32_gpio_and_delay_cb);


    u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in sleep mode after this,
    u8g2_ClearDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0); // wake up display

    ESP_LOGI(TAG, "Display initialised");
    bool drawWifi = true;

    // First display firmware version
    display_draw_info(u8g2);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    int delay = 500;
    display_draw_panel(u8g2, drawWifi, delay);

    // Kill task
    vTaskDelete(NULL);

}

void display_init() {
    xTaskCreate(do_display, "do_display", 4596, NULL, 5, NULL);
}
