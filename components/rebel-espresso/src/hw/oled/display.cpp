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
#include <src/control/power.h>
#include <esp_event.h>
#include <src/control/brew_temp.h>
#include <src/control/boiler_refill.h>

#include "control/controller.h"

#include "events.h"
#include "state.h"

extern "C" {
    #include "u8g2_esp32_hal.h"
}

#define TEMP_ERROR_STR "---"

#define TEMPERATURE_PANEL_WIDTH 112

static esp_event_loop_handle_t s_event_loop;
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
    // H Line just below boiler temp
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

void _render_tec(u8g2_t *u8g2, char *tempBuf, int xpad, int yOffset, int idx) {
    rtd_data_t tec;
    rtds_get(&tec, idx);
    if (tec.fault == Max31865Error::NoError) {
        sprintf(tempBuf, "%.1f", tec.temperature);
    } else {
        sprintf(tempBuf,"%s", TEMP_ERROR_STR);
    }
    u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
    u8g2_DrawStr(u8g2,  (TEMPERATURE_PANEL_WIDTH - u8g2_GetStrWidth(u8g2, tempBuf)) - xpad, yOffset, tempBuf);
}

void display_draw_brew_temp(u8g2_t *u8g2, int *y) {
    rtd_data_t result;
    rtds_get(&result, 1);

    char tempBuf[16];
    auto temp_val = result.temperature;

    // Integral part of temperature, in larger font
    if (result.fault == Max31865Error::NoError) {
        sprintf(tempBuf, "%d", (int) temp_val);
    } else {
        sprintf(tempBuf, TEMP_ERROR_STR);
    }

    u8g2_SetFont(u8g2, u8g2_font_courB24_tf);
    auto width_of_intregral_temp = u8g2_GetStrWidth(u8g2, tempBuf);
    auto x_offset = 0;
    u8g2_DrawStr(u8g2, x_offset, *y, tempBuf);

    // Draw floating point now, as '.x'
    int point = static_cast<int>(temp_val * 10 - static_cast<int>(temp_val) * 10);
    sprintf(tempBuf, ".%d", point);
    u8g2_SetFont(u8g2, u8g2_font_courR10_tf);
    u8g2_DrawStr(u8g2, x_offset + width_of_intregral_temp, *y, tempBuf);

    // Duty
    auto xpad = 3;
    auto yOffset = 8;
    double duty = brew_temp_get_duty();
    sprintf(tempBuf, "%d%%", (int)duty);
    u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
    u8g2_DrawStr(u8g2,  (TEMPERATURE_PANEL_WIDTH - u8g2_GetStrWidth(u8g2, tempBuf)) - xpad, yOffset, tempBuf);
    yOffset += 8 + 2;

    // TEC side 1
    auto idx = 2;
    _render_tec(u8g2, tempBuf, xpad, yOffset, idx);

    // TEC side 2
    yOffset += 8 + 2;
    idx = 3;
    _render_tec(u8g2, tempBuf, xpad, yOffset, idx);
}

void display_draw_boiler_temp(u8g2_t *u8g2, int *y) {
    rtd_data_t result;
    rtds_get(&result, 0);

    char tempBuf[16];
    auto temp_val = result.temperature;

    // Integral part of temperature, in larger font
    if (result.fault == Max31865Error::NoError) {
        sprintf(tempBuf, "%d", (int) temp_val);
    } else {
        sprintf(tempBuf, TEMP_ERROR_STR);
    }


    u8g2_SetFont(u8g2, u8g2_font_courB24_tf);
    auto width_of_intregral_temp = u8g2_GetStrWidth(u8g2, tempBuf);

    auto x_offset = 0;
    u8g2_DrawStr(u8g2, x_offset, *y, tempBuf);

    // Draw floating point now, as '.x'
    int point = static_cast<int>(temp_val * 10 - static_cast<int>(temp_val) * 10);
    sprintf(tempBuf, ".%d", point);
    u8g2_SetFont(u8g2, u8g2_font_courR10_tf);
    u8g2_DrawStr(u8g2, x_offset + width_of_intregral_temp, *y, tempBuf);

    // Duty
    auto xpad = 3;
    auto yOffset = *y - 16;
    double duty = boiler_temp_get_duty();
    sprintf(tempBuf, "%d%%", (int)duty);
    u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
    u8g2_DrawStr(u8g2,  (TEMPERATURE_PANEL_WIDTH - u8g2_GetStrWidth(u8g2, tempBuf) - xpad), yOffset, tempBuf);

    // Water level voltage
    yOffset += 12;
    double level_voltage = boiler_refill_level_mv() / 1e3;
    sprintf(tempBuf, "%.1fV", level_voltage);
    u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
    u8g2_DrawStr(u8g2,  (TEMPERATURE_PANEL_WIDTH - u8g2_GetStrWidth(u8g2, tempBuf) - xpad), yOffset, tempBuf);
}


static void display_draw_info(u8g2_t &u8g2) {
    u8g2_ClearBuffer(&u8g2);

    u8g2_SetFont(&u8g2, u8g2_font_fur11_tf);
    u8g2_DrawStr(&u8g2, 14, 16, "RebelEspresso");

    u8g2_SetFont(&u8g2, u8g2_font_fur11_tf);
    u8g2_DrawStr(&u8g2, 34, 36, "v" FIRMWARE_VERSION);

    u8g2_SetFont(&u8g2, u8g2_font_fur11_tf);
    u8g2_DrawStr(&u8g2, 10, 56, thing_info_id());

    u8g2_SendBuffer(&u8g2);
}

static void display_draw_panel(u8g2_t &u8g2, bool drawWifi, int delay) {
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);

    bool toggle = true;
    while (g_go) {
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

        if (power_is_active()) {
            u8g2_SetPowerSave(&u8g2, 0);
            u8g2_ClearBuffer(&u8g2);

            // Draw temps, flash them when lid is open
            int y = 24;
            display_draw_brew_temp(&u8g2, &y);

            // Now draw frame separator
            y = 32;
            display_draw_frame(&u8g2, TEMPERATURE_PANEL_WIDTH, y);

            y += 32;
            display_draw_boiler_temp(&u8g2, &y);


            int yPosWifi = 14;
            int yPosOnOff = 54;


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
        } else {
            u8g2_SetPowerSave(&u8g2, 1);
        }
        xEventGroupWaitBits(status_event_group, REFRESH_DISPLAY_BIT, true, true, delay / portTICK_PERIOD_MS);
    }
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        xEventGroupSetBits(status_event_group, REFRESH_DISPLAY_BIT);
    } else if (id == POWER_ACTIVE) {
        xEventGroupSetBits(status_event_group, REFRESH_DISPLAY_BIT);
    }
}


static void do_display(void* userData) {
    ESP_LOGI(TAG, "Initialising display");
    u8g2_t u8g2;

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

    //u8g2_Setup_ssd1322_nhd_256x64_f(&u8g2, U8G2_R0, u8g2_esp32_spi_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    u8g2_Setup_ssd1306_128x64_noname_f(&u8g2, U8G2_R0, u8g2_esp32_spi_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in sleep mode after this,
    u8g2_ClearDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0); // wake up display

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, &u8g2));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, &u8g2));


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

void display_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
    xTaskCreate(do_display, "do_display", 4596, NULL, 5, NULL);
}
