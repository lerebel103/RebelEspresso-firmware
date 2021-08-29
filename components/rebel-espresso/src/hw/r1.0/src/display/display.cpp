#include "display.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <thing_info.h>
#include <hw_config.h>
#include "rtds.h"
#include <boiler_temp.h>
#include <cmath>
#include <power.h>
#include <esp_event.h>
#include <brew_tec.h>
#include <boiler_refill.h>
#include <src/sys/wifi_connect.h>
#include <qrcodegen.h>
#include <Max31865.h>

#include "events.h"
#include "state.h"

extern "C" {
#include "u8g2_esp32_hal.h"
}

#define TEMP_ERROR_STR "---"

#define TEMPERATURE_PANEL_WIDTH 112
#define SAVER_CONTRAST 1
#define NORMAL_CONTRAST 255

static esp_event_loop_handle_t s_event_loop;
const static char *TAG = "oled";
static bool g_go = true;
static time_t s_brew_start_time = -1;
static bool s_reset_display = false;
static bool s_show_diag = false;
static bool s_qr_displayed = false;

static float temperature_to_unit(double celcius, units_enum_t unit) {
    if (unit == UNIT_FARENHEIGHT) {
        celcius = celcius * 9 / 5.0f + 32;
    }

    // Round to 1 digit only
    return (int) (round(10 * celcius)) / 10.0f;
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
        u8g2_DrawStr(u8g2, TEMPERATURE_PANEL_WIDTH + 8, yPos + 4, "C");
    } else {
        u8g2_DrawStr(u8g2, TEMPERATURE_PANEL_WIDTH + 8, yPos + 4, "F");
    }
}

void _render_tec(u8g2_t *u8g2, char *tempBuf, int xpad, int yOffset, int idx) {
    reading_t tec;
    rtds_get(&tec, idx);
    if (tec.fault == (uint8_t)RTD_NoError) {
        sprintf(tempBuf, "%.1f", tec.value);
    } else {
        sprintf(tempBuf, "%s", TEMP_ERROR_STR);
    }
    u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
    u8g2_DrawStr(u8g2, (TEMPERATURE_PANEL_WIDTH - u8g2_GetStrWidth(u8g2, tempBuf)) - xpad, yOffset, tempBuf);
}

void _draw_brew_temp_only(u8g2_t *u8g2, const int *y) {
    reading_t result;
    rtds_get(&result, 1);

    char tempBuf[16];
    auto temp_val = result.value;

    // Integral part of temperature, in larger font
    if (result.fault == (uint8_t)RTD_NoError) {
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
}

void display_draw_brew_tec(u8g2_t *u8g2, int *y) {
    char tempBuf[16];
    _draw_brew_temp_only(u8g2, y);

    if (s_show_diag) {
        // Duty
        auto xpad = 3;
        auto yOffset = 8;
        double duty = brew_tec_get_duty();
        sprintf(tempBuf, "%d%%", (int) duty);
        u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
        u8g2_DrawStr(u8g2, (TEMPERATURE_PANEL_WIDTH - u8g2_GetStrWidth(u8g2, tempBuf)) - xpad, yOffset, tempBuf);
        yOffset += 8 + 2;

        // TEC side 1
        auto idx = 2;
        _render_tec(u8g2, tempBuf, xpad, yOffset, idx);

        // TEC side 2
        yOffset += 8 + 2;
        idx = 3;
        _render_tec(u8g2, tempBuf, xpad, yOffset, idx);
    }
}

void display_draw_boiler_temp(u8g2_t *u8g2, int *y) {
    reading_t result;
    rtds_get(&result, RTD_BREW_BOILER_IDX);

    char tempBuf[16];
    auto temp_val = result.value;

    // Integral part of temperature, in larger font
    if (result.fault == (uint8_t)RTD_NoError) {
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

    if (s_show_diag) {
        // Duty
        auto xpad = 3;
        auto yOffset = *y - 20;
        double duty = boiler_temp_get_duty();
        sprintf(tempBuf, "%d%%", (int) duty);
        u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
        u8g2_DrawStr(u8g2, (TEMPERATURE_PANEL_WIDTH - u8g2_GetStrWidth(u8g2, tempBuf) - xpad), yOffset, tempBuf);

        yOffset += 8 + 2;
        double actual_setpoint = boiler_temp_get_trimmed_setpoint();
        sprintf(tempBuf, "%.1f", actual_setpoint);
        u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
        u8g2_DrawStr(u8g2, (TEMPERATURE_PANEL_WIDTH - u8g2_GetStrWidth(u8g2, tempBuf) - xpad), yOffset, tempBuf);

        // Water level voltage
        yOffset += 8 + 2;
        double level_voltage = boiler_refill_level_mv() / 1e3;
        sprintf(tempBuf, "%.1fV", level_voltage);
        u8g2_SetFont(u8g2, u8g2_font_courR08_tf);
        u8g2_DrawStr(u8g2, (TEMPERATURE_PANEL_WIDTH - u8g2_GetStrWidth(u8g2, tempBuf) - xpad), yOffset, tempBuf);
    }
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

void _draw_brew_counter(u8g2_t &u8g2) {
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_ClearBuffer(&u8g2);

    char buf[64];
    sprintf(buf, "%d", (int) (pdTICKS_TO_MS(xTaskGetTickCount()) / 1000 - s_brew_start_time));

    u8g2_SetFont(&u8g2, u8g2_font_courB24_tf);
    int w = u8g2_GetStrWidth(&u8g2, buf);
    u8g2_DrawStr(&u8g2, (128 - w) / 2 - 6, 32, buf);
    u8g2_SetFont(&u8g2, u8g2_font_courB12_tf);
    u8g2_DrawStr(&u8g2, (128 - w) / 2 + w + 1, 32, "s");

    u8g2_SendBuffer(&u8g2);
}

static void esp_qrcode_print(u8g2_t &u8g2, int x_off, int y_off, const uint8_t* qrcode, int size)
{
    // Draw a square for the QR with a white border
    int border = 1;
    for (int y = -border; y < size + border ; y+=1) {
        for (int x = -border; x < size + border ; x+=1) {

            if (qrcodegen_getModule(qrcode, x, y) and x >= 0 and y >= 0 and x < size and y < size) {
                u8g2_SetDrawColor(&u8g2, 0);
            } else {
                u8g2_SetDrawColor(&u8g2, 1);
            }
            u8g2_DrawPixel(&u8g2, x + x_off, y + y_off);
        }
    }
    u8g2_SetDrawColor(&u8g2, 1);
}


void _draw_provisioning(u8g2_t &u8g2) {
    if (s_qr_displayed) {
        return;
    }
    s_qr_displayed = true;

    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_ClearBuffer(&u8g2);

    u8g2_SetFont(&u8g2, u8g2_font_helvB08_tr);
    const char *line1 = "Scan to provision";
    int w = u8g2_GetStrWidth(&u8g2, line1);
    u8g2_DrawStr(&u8g2, (128 - w) / 2, 10, line1);

    int xPos = 45;
    int yPos = 17;
    esp_qrcode_print(u8g2, xPos, yPos, wifi_get_prov_qr(), wifi_get_prov_qr_len());

    const char* line2 = thing_info_id();
    w = u8g2_GetStrWidth(&u8g2, line2);
    u8g2_DrawStr(&u8g2, (128 - w) / 2, 64, line2);

    u8g2_SendBuffer(&u8g2);

}

void _draw_descale_mode(u8g2_t &u8g2) {
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_ClearBuffer(&u8g2);

    u8g2_SetFont(&u8g2, u8g2_font_courR10_tf);
    const char *line1 = "Descaling";
    int w = u8g2_GetStrWidth(&u8g2, line1);
    u8g2_DrawStr(&u8g2, (128 - w) / 2, 16, line1);
    const char *line2 = "mode";
    w = u8g2_GetStrWidth(&u8g2, line2);
    u8g2_DrawStr(&u8g2, (128 - w) / 2, 16 + 22, line2);

    u8g2_SendBuffer(&u8g2);
}

void _draw_active_mode(u8g2_t &u8g2, bool drawWifi) {
    static bool toggle = true;

    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_ClearBuffer(&u8g2);

    // Draw temps, flash them when lid is open
    int y = 24;
    display_draw_brew_tec(&u8g2, &y);

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
}

/* more or less generic setup of all these small OLEDs */
static const uint8_t u8x8_d_ssd1306_128x64_noname_init_seq[] = {

        U8X8_START_TRANSFER(),                /* enable chip, delay is part of the transfer start */


        U8X8_C(0x0ae),                        /* display off */
        U8X8_CA(0x0d5, 0x080),        /* clock divide ratio (0x00=1) and oscillator frequency (0x8) */
        U8X8_CA(0x0a8, 0x03f),        /* multiplex ratio */
        U8X8_CA(0x0d3, 0x000),        /* display offset */
        U8X8_C(0x040),                        /* set display start line to 0 */
        U8X8_CA(0x08d,
                0x014),        /* [2] charge pump setting (p62): 0x014 enable, 0x010 disable, SSD1306 only, should be removed for SH1106 */
        U8X8_CA(0x020, 0x000),        /* page addressing mode */

        U8X8_C(0x0a1),                /* segment remap a0/a1*/
        U8X8_C(0x0c8),                /* c0: scan dir normal, c8: reverse */
        // Flipmode
        // U8X8_C(0x0a0),				/* segment remap a0/a1*/
        // U8X8_C(0x0c0),				/* c0: scan dir normal, c8: reverse */

        U8X8_CA(0x0da,
                0x012),        /* com pin HW config, sequential com pin config (bit 4), disable left/right remap (bit 5) */

        U8X8_CA(0x081, 0x0cf),        /* [2] set contrast control 0x0cf */
        U8X8_CA(0x0d9, 0x20),        /* [2] pre-charge period 0x022/f1*/
        U8X8_CA(0x0db, 0),        /* vcomh deselect level 0x040 */

        U8X8_C(0x02e),                /* Deactivate scroll */
        U8X8_C(0x0a4),                /* output ram to display */
        U8X8_C(0x0a6),                /* none inverted normal display mode */

        U8X8_END_TRANSFER(),                /* disable chip */
        U8X8_END()                        /* end of sequence */
};


static void display_draw_panel(u8g2_t &u8g2, bool drawWifi, int delay) {
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);

    int time_in_high_contrast = 0;
    while (g_go) {
        TickType_t  startTick = xTaskGetTickCount();

        EventBits_t uxBits = xEventGroupWaitBits(
                status_event_group, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT | DESCALE_MODE_BIT | PROVISIONING_BIT, false, true, 0);

        if (s_reset_display) {
            // For some reason the screen will often go into inverse contrast mode, hope this cures it.
            u8x8_cad_SendSequence(&u8g2.u8x8, u8x8_d_ssd1306_128x64_noname_init_seq);
            s_reset_display = false;
            s_qr_displayed = false;
            time_in_high_contrast = 0;
        }

        if (PROVISIONING_BIT & uxBits) {
            time_in_high_contrast = 0;
            _draw_provisioning(u8g2);
        } else if (DESCALE_MODE_BIT & uxBits) {
            time_in_high_contrast = 0;
            _draw_descale_mode(u8g2);
        } else if (s_brew_start_time >= 0) {
            time_in_high_contrast = 0;
            _draw_brew_counter(u8g2);
            delay = 200;
        } else if (power_is_active()) {

            // Always in high contrast when brew is running
            if (s_brew_start_time > 0) {
                time_in_high_contrast = 0;
            }

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

            _draw_active_mode(u8g2, drawWifi);

        } else {
            time_in_high_contrast = 0;
            u8g2_ClearDisplay(&u8g2);
            u8g2_SetPowerSave(&u8g2, 1);
        }

        // Set contrast with screen saver
        if (time_in_high_contrast < 60) {
            u8g2_SetContrast(&u8g2, NORMAL_CONTRAST);
        } else {
            u8g2_SetContrast(&u8g2, SAVER_CONTRAST);
        }

        xEventGroupWaitBits(status_event_group, REFRESH_DISPLAY_BIT, true, true, delay / portTICK_PERIOD_MS);
        time_in_high_contrast += ((xTaskGetTickCount()-startTick)*portTICK_PERIOD_MS) / 1000;
    }
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        xEventGroupSetBits(status_event_group, REFRESH_DISPLAY_BIT);
    } else if (id == POWER_ACTIVE) {
        s_brew_start_time = -1;
        s_reset_display = true;
        xEventGroupSetBits(status_event_group, REFRESH_DISPLAY_BIT);
    }
}

static void _brew_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (!(xEventGroupGetBits(status_event_group) & DESCALE_MODE_BIT)) {
        if (id == BREW_STARTED) {
            s_brew_start_time = pdTICKS_TO_MS(xTaskGetTickCount()) / 1000;
        } else if (id == BREW_STOPPED) {
            s_brew_start_time = -1;
        }
    }
}


static void do_display(void *userData) {
    ESP_LOGI(TAG, "Initialising display");
    u8g2_t u8g2;

    u8g2_esp32_hal_t u8g2_esp32_hal = {};
    u8g2_esp32_hal.sda = GPIO_NUM_NC;
    u8g2_esp32_hal.scl = GPIO_NUM_NC;
    u8g2_esp32_hal.mosi = PIN_MOSI;
    u8g2_esp32_hal.miso = PIN_MISO;
    u8g2_esp32_hal.clk = PIN_SCK;
    u8g2_esp32_hal.cs = PIN_OUT_DISPLAY_CS;
    u8g2_esp32_hal.reset = PIN_OUT_ADC_RESET;
    u8g2_esp32_hal.dc = PIN_OUT_DISPLAY_DC;

    u8g2_esp32_hal_init(u8g2_esp32_hal);

    //u8g2_Setup_ssd1322_nhd_256x64_f(&u8g2, U8G2_R0, u8g2_esp32_spi_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    //u8g2_Setup_st77
    u8g2_Setup_ssd1306_128x64_noname_f(&u8g2, U8G2_R0, u8g2_esp32_spi_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in sleep mode after this,
    u8g2_ClearDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0); // wake up display

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, &u8g2));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, &u8g2));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BREW_STARTED,
                                                    _brew_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BREW_STOPPED,
                                                    _brew_events, s_event_loop));


    ESP_LOGI(TAG, "Display initialised");
    bool drawWifi = true;

    s_reset_display = true;

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
