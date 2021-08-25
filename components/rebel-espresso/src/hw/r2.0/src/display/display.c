#include "display.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event.h>

#include <hal/gpio_types.h>
#include <esp_spiffs.h>
#include <dirent.h>
#include <fontx.h>
#include <ili9340.h>

#include "hw_config.h"
#include "events.h"

#define TAG "tft"

static esp_event_loop_handle_t s_event_loop;
static time_t s_brew_start_time = -1;
static TFT_t dev;
static uint16_t model;


static void SPIFFS_Directory(char * path) {
    DIR* dir = opendir(path);
    assert(dir != NULL);
    while (true) {
        struct dirent*pe = readdir(dir);
        if (!pe) break;
        ESP_LOGI(__FUNCTION__,"d_name=%s d_ino=%d d_type=%x", pe->d_name,pe->d_ino, pe->d_type);
    }
    closedir(dir);
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == POWER_STANDBY) {
        xEventGroupSetBits(status_event_group, REFRESH_DISPLAY_BIT);
        lcdDisplayOff(&dev);
        lcdBacklightOff(&dev);
    } else if (id == POWER_ACTIVE) {
        s_brew_start_time = -1;
        xEventGroupSetBits(status_event_group, REFRESH_DISPLAY_BIT);
        lcdDisplayOn(&dev);
        lcdBacklightOn(&dev);
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

TickType_t ColorBarTest(TFT_t * dev, int width, int height) {
    TickType_t startTick, endTick, diffTick;
    startTick = xTaskGetTickCount();

    if (width < height) {
        uint16_t y1,y2;
        y1 = height/3;
        y2 = (height/3)*2;
        lcdDrawFillRect(dev, 0, 0, width-1, y1-1, RED);
        vTaskDelay(1);
        lcdDrawFillRect(dev, 0, y1-1, width-1, y2-1, GREEN);
        vTaskDelay(1);
        lcdDrawFillRect(dev, 0, y2-1, width-1, height-1, BLUE);
    } else {
        uint16_t x1,x2;
        x1 = width/3;
        x2 = (width/3)*2;
        lcdDrawFillRect(dev, 0, 0, x1-1, height-1, RED);
        vTaskDelay(1);
        lcdDrawFillRect(dev, x1-1, 0, x2-1, height-1, GREEN);
        vTaskDelay(1);
        lcdDrawFillRect(dev, x2-1, 0, width-1, height-1, BLUE);
    }

    endTick = xTaskGetTickCount();
    diffTick = endTick - startTick;
    ESP_LOGI(__FUNCTION__, "elapsed time[ms]:%d",diffTick*portTICK_RATE_MS);
    return diffTick;
}

#include <string.h>

TickType_t ArrowTest(TFT_t * dev, FontxFile *fx, int width, int height) {
    TickType_t startTick, endTick, diffTick;
    startTick = xTaskGetTickCount();

    // get font width & height
    uint8_t buffer[FontxGlyphBufSize];
    uint8_t fontWidth;
    uint8_t fontHeight;
    GetFontx(fx, 0, buffer, &fontWidth, &fontHeight);
    ESP_LOGD(__FUNCTION__,"fontWidth=%d fontHeight=%d",fontWidth,fontHeight);

    uint16_t xpos;
    uint16_t ypos;
    int	stlen;
    uint8_t ascii[24];
    uint16_t color;

    lcdFillScreen(dev, BLACK);

    if (model == 0x9225) strcpy((char *)ascii, "ILI9225");
    if (model == 0x9226) strcpy((char *)ascii, "ILI9225G");
    if (model == 0x9340) strcpy((char *)ascii, "ILI9340");
    if (model == 0x9341) strcpy((char *)ascii, "ILI9341");
    if (model == 0x7735) strcpy((char *)ascii, "ST7735");
    if (model == 0x7796) strcpy((char *)ascii, "ST7796S");
    if (width < height) {
        xpos = ((width - fontHeight) / 2) - 1;
        ypos = (height - (strlen((char *)ascii) * fontWidth)) / 2;
        lcdSetFontDirection(dev, DIRECTION90);
    } else {
        ypos = ((height - fontHeight) / 2) - 1;
        xpos = (width - (strlen((char *)ascii) * fontWidth)) / 2;
        lcdSetFontDirection(dev, DIRECTION0);
    }
    color = WHITE;
    lcdDrawString(dev, fx, xpos, ypos, ascii, color);

    lcdSetFontDirection(dev, 0);
    //lcdFillScreen(dev, WHITE);
    color = RED;
    lcdDrawFillArrow(dev, 10, 10, 0, 0, 5, color);
    strcpy((char *)ascii, "0,0");
    lcdDrawString(dev, fx, 0, 30, ascii, color);

    color = GREEN;
    lcdDrawFillArrow(dev, width-11, 10, width-1, 0, 5, color);
    //strcpy((char *)ascii, "79,0");
    sprintf((char *)ascii, "%d,0",width-1);
    stlen = strlen((char *)ascii);
    xpos = (width-1) - (fontWidth*stlen);
    lcdDrawString(dev, fx, xpos, 30, ascii, color);

    color = GRAY;
    lcdDrawFillArrow(dev, 10, height-11, 0, height-1, 5, color);
    //strcpy((char *)ascii, "0,159");
    sprintf((char *)ascii, "0,%d",height-1);
    ypos = (height-11) - (fontHeight) + 5;
    lcdDrawString(dev, fx, 0, ypos, ascii, color);

    color = CYAN;
    lcdDrawFillArrow(dev, width-11, height-11, width-1, height-1, 5, color);
    //strcpy((char *)ascii, "79,159");
    sprintf((char *)ascii, "%d,%d",width-1, height-1);
    stlen = strlen((char *)ascii);
    xpos = (width-1) - (fontWidth*stlen);
    lcdDrawString(dev, fx, xpos, ypos, ascii, color);

    endTick = xTaskGetTickCount();
    diffTick = endTick - startTick;
    ESP_LOGI(__FUNCTION__, "elapsed time[ms]:%d",diffTick*portTICK_RATE_MS);
    return diffTick;
}

void _tft_loop(void * arg) {
    // set font file
    spi_device_acquire_bus(dev._SPIHandle, portMAX_DELAY);
    FontxFile fx16G[2];
    FontxFile fx24G[2];
    FontxFile fx32G[2];
    InitFontx(fx16G,"/spiffs/ILGH16XB.FNT",""); // 8x16Dot Gothic
    InitFontx(fx24G,"/spiffs/ILGH24XB.FNT",""); // 12x24Dot Gothic
    InitFontx(fx32G,"/spiffs/ILGH32XB.FNT",""); // 16x32Dot Gothic

    FontxFile fx16M[2];
    FontxFile fx24M[2];
    FontxFile fx32M[2];
    InitFontx(fx16M,"/spiffs/ILMH16XB.FNT",""); // 8x16Dot Mincyo
    InitFontx(fx24M,"/spiffs/ILMH24XB.FNT",""); // 12x24Dot Mincyo
    InitFontx(fx32M,"/spiffs/ILMH32XB.FNT",""); // 16x32Dot Mincyo
    spi_device_release_bus(dev._SPIHandle);

    do {
            spi_device_acquire_bus(dev._SPIHandle, portMAX_DELAY);
            ColorBarTest(&dev, CONFIG_WIDTH, CONFIG_HEIGHT);
            spi_device_release_bus(dev._SPIHandle);
            vTaskDelay(100);

            spi_device_acquire_bus(dev._SPIHandle, portMAX_DELAY);
            ArrowTest(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT);
            spi_device_release_bus(dev._SPIHandle);
            vTaskDelay(100);
    } while(1);

}


void display_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;

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
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total,&used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,"Failed to get SPIFFS partition information (%s)",esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG,"Partition size: total: %d, used: %d", total, used);
    }

    SPIFFS_Directory("/spiffs/");

    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_STANDBY,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, POWER_ACTIVE,
                                                    _power_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BREW_STARTED,
                                                    _brew_events, s_event_loop));
    ESP_ERROR_CHECK(esp_event_handler_register_with(s_event_loop, MACHINE_EVENTS, BREW_STOPPED,
                                                    _brew_events, s_event_loop));


    spi_master_init(&dev,
                    PIN_MOSI,
                    PIN_SCK,
                    PIN_OUT_DISPLAY_CS,
                    PIN_OUT_DISPLAY_DC,
                    PIN_OUT_DISPLAY_RESET,
                    PIN_OUT_DISPLAY_LED);
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
    spi_device_acquire_bus(dev._SPIHandle, portMAX_DELAY);
    lcdInit(&dev, model, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETX, CONFIG_OFFSETY);
    spi_device_release_bus(dev._SPIHandle);

#if CONFIG_INVERSION
    ESP_LOGI(TAG, "Enable Display Inversion");
	lcdInversionOn(&dev);
#endif

#if CONFIG_RGB_COLOR
    ESP_LOGI(TAG, "Change BGR filter to RGB filter");
	lcdBGRFilter(&dev);
#endif

    ESP_LOGI(TAG, "########### TFT READY ###########");

    xTaskCreate(_tft_loop, "tft_loop", 1024*6, NULL, 2, NULL);
}