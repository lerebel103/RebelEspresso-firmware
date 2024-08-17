#include "display.h"
#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event.h>
#include <src/hw/base/rtds.h>
#include <src/hw/base/boiler_refill.h>
#include <src/hw/base/boiler_temp.h>
#include <src/hw/base/brew_temp.h>
#include <src/thing_info.h>
#include <version.h>
#include <src/hw/base/power.h>
#include <src/hw/base/boiler_refill_states.h>
#include <cmath>
#include <src/hw/base/hw_specs.h>

extern "C" {
#include <hal/gpio_types.h>
#include <esp_spiffs.h>
#include <dirent.h>
#include <fontx.h>
#include <ili9340.h>
}

#include "hw_config.h"
#include "events.h"
#include "../../../../../../../managed_components/espressif__qrcode/qrcodegen.h"
#include "wifi/wifi_connect.h"

#define TAG "tft"

#define TEMP_ERROR_STR "---"


static time_t s_brew_start_time = -1;
static TFT_t dev;
static uint16_t model;
static bool s_on = false;
static bool s_last_on_state = false;
static bool s_go = false;
static bool s_qr_displayed = false;


static void SPIFFS_Directory(const char *path) {
    DIR *dir = opendir(path);
    assert(dir != NULL);
    while (true) {
        struct dirent *pe = readdir(dir);
        if (!pe) break;
        ESP_LOGI(__FUNCTION__, "d_name=%s d_ino=%d d_type=%x", pe->d_name, pe->d_ino, pe->d_type);
    }
    closedir(dir);
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        s_on = false;
        xEventGroupSetBits(status_event_group, REFRESH_DISPLAY_BIT);
    } else if (id == POWER_ACTIVE) {
        s_on = true;
        s_qr_displayed = false;
        s_brew_start_time = -1;
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


int _draw_temperature(const reading_t &result, FontxFile *fx1, FontxFile *fx2, int x, int y, uint16_t color) {
    double temp_val = ((int)(result.value * 100 + .5) / 100.0);
    const static int len = 16;
    char tempBuf[len];

    // Integral part of temperature, in larger font
    if (result.fault == RTD_NoError) {
        sprintf(tempBuf, "%3d", (int) temp_val);
    } else {
        sprintf(tempBuf, "%s", TEMP_ERROR_STR);
    }
    int char_width = 32;

    // Draw integral part
    lcdDrawString(&dev, fx1, x, y, (uint8_t *) tempBuf, color);
    int width = strlen(tempBuf) * char_width;

    // Draw floating point now, as '.x'
    if (result.fault == RTD_NoError) {
        int point = static_cast<int>(temp_val * 10 - static_cast<int>(temp_val) * 10);
        sprintf(tempBuf, ".%d", point);
    } else {
        sprintf(tempBuf, ".-");
    }
    lcdDrawString(&dev, fx2, x + width, y - 3, (uint8_t *) tempBuf, color);
    return x + width + char_width + 5;
}

void _draw_setpoint(double setpoint, FontxFile *fx1, int x, int y, uint16_t color) {
    const static int len = 16;
    char tempBuf[len];
    sprintf(tempBuf, "/%.1f", setpoint);
    lcdDrawString(&dev, fx1, x, y, (uint8_t *) tempBuf, color);
}

void _draw_duty(int duty, FontxFile *fx1, int x, int y, uint16_t color) {
    const static int len = 16;
    char tempBuf[len];
    sprintf(tempBuf, "%3d%%", duty);
    lcdDrawString(&dev, fx1, x, y, (uint8_t *) tempBuf, color);
}

void _display_info(FontxFile *fx1) {
    uint8_t x = 12;
    uint8_t y = 40;
    lcdDrawString(&dev, fx1, x, y, (uint8_t *) "RebelEspresso", GREEN);

    y += 32;
    lcdDrawString(&dev, fx1, x, y, (uint8_t *) ("v" FIRMWARE_VERSION), WHITE);

    y += 32;
    lcdDrawString(&dev, fx1, x, y, (uint8_t *) thing_info_id(), WHITE);
}

static void _qrcode_print(int x_off, int y_off, const uint8_t *qrcode, int size) {
    if (s_qr_displayed) {
        return;
    }

    lcdFillScreen(&dev, BLACK);

    int qr_pixel = 4;

    // Draw a square for the QR with a white border
    int x1;
    int x2;
    int y1;
    int y2;
    int border = 1;
    for (int y = -border; y < size + border; y += 1) {
        for (int x = -border; x < size + border; x += 1) {
            x1 = x * qr_pixel + x_off;
            y1 = y * qr_pixel + y_off;
            x2 = x1 + qr_pixel;
            y2 = y1 + qr_pixel;
            if (qrcodegen_getModule(qrcode, x, y) and x >= 0 and y >= 0 and x < size and y < size) {
                lcdDrawFillRect(&dev, x1, y1, x2, y2, BLACK);
            } else {
                lcdDrawFillRect(&dev, x1, y1, x2, y2, WHITE);
            }
        }
    }

    s_qr_displayed = true;
}


void _ensure_power_state_ok() {
    // look at last event received
    bool on = s_on;
    if (s_last_on_state != on) {
        if (s_on) {
#if CONFIG_ILI9225
            model = 0x9225;
#endif
#if CONFIG_ILI9225G
            model = 0x9226;
#endif
#if CONFIG_ILI9340
            model = 0x9340;
#endif
#if CONFIG_ILI9341
            model = 0x9341;
#endif
#if CONFIG_ST7735
            model = 0x7735;
#endif
#if CONFIG_ST7796
            model = 0x7796;
#endif
            lcdInit(&dev, model, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETY, CONFIG_OFFSETX);
            lcdDisplayOn(&dev);
            lcdFillScreen(&dev, BLACK);
            lcdSetFontFill(&dev, BLACK);
            lcdSetBrightness(100);
            lcdBacklightOn(&dev);
        } else {
            lcdDisplayOff(&dev);
            lcdBacklightOff(&dev);
            s_qr_displayed = false;
        }
        s_last_on_state = on;
    }
}

static void _draw_active(FontxFile *fx0, FontxFile *fx16M, FontxFile *fx32M) {
    TickType_t startTick = xTaskGetTickCount();
    static bool show_circle = true;
    reading_t result = {};

    int header_width = 26;

    if (show_circle) {
        lcdDrawFillCircle(&dev, 5, 10, 5, GREEN);
    } else {
        lcdDrawFillCircle(&dev, 5, 10, 5, BLACK);
    }
    show_circle = !show_circle;

    _draw_duty(boiler_temp_get_duty(), fx0, 20, 24, WHITE);

    // Draw WiFi symbol
    if (xEventGroupGetBits(status_event_group) & WIFI_CONNECTED_BIT) {
        lcdDrawCircle(&dev, CONFIG_WIDTH, 0, 4, WHITE);
        lcdDrawCircle(&dev, CONFIG_WIDTH, 0, 9, WHITE);
        lcdDrawCircle(&dev, CONFIG_WIDTH, 0, 14, WHITE);
    } else {
        lcdDrawFillRect(&dev, CONFIG_WIDTH - 11, 0, CONFIG_HEIGHT, 11, BLACK);
    }

    // Draw mqtt connection
    int x2 = CONFIG_WIDTH - 32;
    int y2 = 10;
    if (xEventGroupGetBits(status_event_group) & CORE_MQTT_CLIENT_CONNECTED_BIT) {
        lcdDrawTriangle(&dev, x2 - 14, y2, 14, 14, 0, WHITE);
        lcdDrawTriangle(&dev, x2, y2, 14, 14, 180, WHITE);
    } else {
        lcdDrawFillRect(&dev, x2 - 14, y2, x2 + 14, 14, BLACK);
    }

    // Header separator
    lcdDrawFillRect(&dev, 0, header_width, CONFIG_WIDTH - 1, header_width, GRAY);

    int x = 4;
    int y = 16;

    int vert_space = (240-header_width) / 2;

    int voffset = 18;
    y += vert_space + 3 - voffset;
    rtds_get(&result, RTD_BREW_HEAD_IDX);
    //result.value = 88.3;
    //result.fault = RTD_NoError;
    double brew_temp = result.value;
    double brew_setpoint = brew_temp_get_setpoint();

    // Status
    int brew_color = CYAN;
    if (abs(brew_temp - brew_setpoint) < 1.5) {
        brew_color = GREEN;
    } else {
        if (brew_temp > brew_setpoint) {
            brew_color = RED;
        }
    }
    int last_pos = _draw_temperature(result, fx32M, fx16M, x, y, brew_color);
    _draw_setpoint(brew_setpoint, fx16M, last_pos, y - 3, SETPOINT_COLOR);
    lcdDrawString(&dev, fx0, last_pos + 20, y - 38, (uint8_t *) "Brew", GRAY);
    lcdDrawFillRect(&dev, 0, y + 2 + voffset, CONFIG_WIDTH - 1, y + 2 + voffset, GRAY);

    y += vert_space;
    static bool boiler_error_message = false;
    if (boiler_refill_state() == REFILL_STATE_ERROR) {
        if (!boiler_error_message) {
            lcdDrawFillRect(&dev, 0, y - vert_space + 24, CONFIG_WIDTH - 1, y + 16, BLACK);
        }
        boiler_error_message = true;
        lcdDrawString(&dev, fx16M, x + 20, y - 16, (uint8_t *) "Refill Error", RED);
    } else {
        if (boiler_error_message) {
            lcdDrawFillRect(&dev, 0, y - vert_space + 24, CONFIG_WIDTH - 1, y + 16, BLACK);
        }
        boiler_error_message = false;
        // All OK
        rtds_get(&result, RTD_BREW_BOILER_IDX);
        double actual_setpoint = boiler_temp_get_current_setpoint();
        last_pos = _draw_temperature(result, fx32M, fx16M, x, y, WHITE);
        _draw_setpoint(actual_setpoint, fx16M, last_pos, y - 3, SETPOINT_COLOR);
        lcdDrawString(&dev, fx0, last_pos + 20, y - 38, (uint8_t *) "Boiler", GRAY);
        lcdDrawFillRect(&dev, 0, y + 1 + voffset, CONFIG_WIDTH - 1, y + 1 + voffset, GRAY);
    }

/*    x = 5;
    y += 10 + 24;
    // Duty

    y += 24 + 5;
    // Water level voltage
    double level_voltage = boiler_refill_level_mv() / 1e3;
    sprintf(tempBuf, "Level: %.1fV", level_voltage);
    lcdDrawString(&dev, fx0, x, y, (uint8_t *) tempBuf, WHITE);
*/
    /*// Draw border around duty / water level voltage
    y = y - 22;
    lcdDrawFillRect(&dev, CONFIG_WIDTH - 9 * 4 + 2, y + 1 - 18, CONFIG_WIDTH - 9 * 4 + 2, y + 1, GRAY);


     */

    TickType_t endTick = xTaskGetTickCount();
    ESP_LOGI(TAG, "Render Took %" PRIu32 "ms\r\n", pdTICKS_TO_MS(endTick - startTick));
}

static void _draw_provisioning(FontxFile *fx16M) {
    int x = 52;
    int y = 25;

    lcdDrawString(&dev, fx16M, x, y, (uint8_t *) "Scan to set", WHITE);
    lcdDrawString(&dev, fx16M, x + 36, y + 28, (uint8_t *) "WiFi", WHITE);

    int xPos = x;
    int yPos = 65;
    _qrcode_print(xPos, yPos, wifi_get_prov_qr(), wifi_get_prov_qr_len());
}

static void _draw_descale_mode(FontxFile *fx) {
    int x = 36;
    int y = 108 + 24;

    lcdDrawString(&dev, fx, x, y, (uint8_t *) "Descaling Mode", WHITE);
}

static void _draw_refill_water_tank(FontxFile *fx) {
    int x = 32;
    int y = 108 + 24;

    lcdDrawString(&dev, fx, x, y, (uint8_t *) "Refill Tank", RED);
}

static void _draw_brew_counter(FontxFile *fx1, FontxFile *fx2) {
    char buf[64];
    int seconds = (int) (pdTICKS_TO_MS(xTaskGetTickCount()) / 1000 - s_brew_start_time);
    sprintf(buf, "%ds", seconds);

    int num_chars = 2;
    if (seconds >= 10) {
        num_chars += 1;
    } else if (num_chars >= 100) {
        num_chars += 2;
    }

    int x = (CONFIG_WIDTH - num_chars * 32) / 2;
    int y = 84 + 64;
    lcdDrawString(&dev, fx2, x, y, (uint8_t *) buf, WHITE);
}

void _tft_loop(void *arg) {
    // set font file
    FontxFile fx16G[2];
    FontxFile fx24G[2];
    FontxFile fx32G[2];
    InitFontx(fx16G, "/spiffs/ILGH16XB.FNT", ""); // 8x16Dot Gothic
    InitFontx(fx24G, "/spiffs/ILGH24XB.FNT", ""); // 12x24Dot Gothic
    InitFontx(fx32G, "/spiffs/ILGH32XB.FNT", ""); // 16x32Dot Gothic

    FontxFile fx16M[2];
    FontxFile fx24M[2];
    FontxFile fx32M[2];
    FontxFile fx64M[2];
    InitFontx(fx16M, "/spiffs/ILMH16XB.FNT", ""); // 8x16Dot Mincyo
    InitFontx(fx24M, "/spiffs/ILMH24XB.FNT", ""); // 12x24Dot Mincyo
    InitFontx(fx32M,"/spiffs/ILMH32XB.FNT",""); // 16x32Dot Mincyo
    InitFontx(fx64M, "/spiffs/ILMH64XB.FNT", ""); // 32x64Dot Mincyo


    // Display info, but turn on first
    s_on = true;
    _ensure_power_state_ok();
    _display_info(fx24G);
    vTaskDelay(2000 / portTICK_PERIOD_MS);


    int delay = 1000;
    int state;
    int last_state = 0;
    while (s_go) {
        EventBits_t uxBits = xEventGroupWaitBits(
                status_event_group, WIFI_CONNECTED_BIT | CORE_MQTT_CLIENT_CONNECTED_BIT | DESCALE_MODE_BIT | PROVISIONING_BIT,
                false, true, 0);
        _ensure_power_state_ok();

        if (PROVISIONING_BIT & uxBits) {
            state = 1;
            if (state != last_state) {
                lcdFillScreen(&dev, BLACK);
                last_state = state;
                s_qr_displayed = false;
            }
            _draw_provisioning(fx24M);
        } else if (DESCALE_MODE_BIT & uxBits) {
            state = 2;
            if (state != last_state) {
                lcdFillScreen(&dev, BLACK);
                last_state = state;
            }
            _draw_descale_mode(fx24M);
        } else if (hw_specs_is_aux_in_activated()) {
            state = 3;
            if (state != last_state) {
                lcdFillScreen(&dev, BLACK);
                last_state = state;
            }
            _draw_refill_water_tank(fx32M);
            delay = 500;
        } else if (s_brew_start_time >= 0) {
            state = 4;
            if (state != last_state) {
                lcdFillScreen(&dev, BLACK);
                last_state = state;
            }
            _draw_brew_counter(fx32M, fx64M);
            delay = 500;
        } else if (power_is_active()) {
            state = 5;
            if (state != last_state) {
                lcdFillScreen(&dev, BLACK);
                last_state = state;
            }
            delay = 1000;
            _draw_active(fx24M, fx32M, fx64M);
        }

        vTaskDelay(delay / portTICK_PERIOD_MS);
    }
}


void display_init() {


    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
            .base_path = "/spiffs",
            .partition_label = NULL,
            .max_files = 10,
            .format_if_mount_failed =true
    };

    // Use settings defined above toinitialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is anall-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    SPIFFS_Directory("/spiffs/");

    ESP_ERROR_CHECK(esp_event_handler_register( MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register( MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register( MACHINE_EVENTS, BREW_STARTED,
                                                    _brew_events, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register( MACHINE_EVENTS, BREW_STOPPED,
                                                    _brew_events, nullptr));


    spi_master_init(&dev,
                    PIN_MOSI,
                    PIN_SCK,
                    PIN_OUT_DISPLAY_CS,
                    PIN_OUT_DISPLAY_DC,
                    PIN_OUT_DISPLAY_RESET,
                    PIN_OUT_DISPLAY_LED);

    ESP_LOGI(TAG, "########### TFT READY ###########");
    s_go = true;
    xTaskCreate(_tft_loop, "tft_loop", 1024 * 5, NULL, 2, NULL);
}