#include "display.h"
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event.h>
#include "rtds.h"
#include <src/machine/boiler_refill.h>
#include <src/machine/boiler_temp.h>
#include <src/machine/brew_temp.h>
#include <src/device/thing_info.h>
#include <version.h>
#include <src/machine/power.h>
#include <src/machine/boiler_refill_states.h>
#include <src/runtime/process_image.h>
#include <src/machine/water_probe.h>
#include <cmath>
#include "hw_specs.h"

extern "C" {
#include <hal/gpio_types.h>
#include <esp_spiffs.h>
#include <dirent.h>
#include <fontx.h>
#include <ili9340.h>
}

#include "hw_config.h"
#include "events.h"
#include "wifi/wifi_manager.h"
#include "wifi/wifi_ap.h"
#include "web_auth.h"
#include "brew.h"
#include "mqtt/mqtt_ha.h"
#include "homekit/homekit.h"

#define TAG "tft"

#define TEMP_ERROR_STR "---"

static time_t s_brew_start_time = -1;
static TFT_t dev;
static uint16_t model;
static bool s_on = false;
static bool s_last_on_state = false;
static FontxFile fx16G[2];
static FontxFile fx24G[2];
static FontxFile fx32G[2];
static FontxFile fx16M[2];
static FontxFile fx24M[2];
static FontxFile fx32M[2];
static FontxFile fx64M[2];
static int last_state = 0;
static int64_t s_wifi_setup_overlay_deadline_us = 0;
static int s_last_wifi_icon_state = -1;
static int s_last_mqtt_icon_state = -1;
static int s_last_homekit_icon_state = -1;
static bool s_force_status_icon_redraw = true;

static constexpr int64_t WIFI_SETUP_OVERLAY_DURATION_US = 120LL * 1000000LL;

static int _wifi_setup_overlay_seconds_remaining() {
  if (s_wifi_setup_overlay_deadline_us <= 0) {
    return 0;
  }

  int64_t remaining_us = s_wifi_setup_overlay_deadline_us - esp_timer_get_time();
  if (remaining_us <= 0) {
    return 0;
  }

  // Round up so the countdown reads 2:00 initially instead of 1:59.
  return static_cast<int>((remaining_us + 999999LL) / 1000000LL);
}

static void SPIFFS_Directory(const char *path) {
  DIR *dir = opendir(path);
  assert(dir != NULL);
  while (true) {
    struct dirent *pe = readdir(dir);
    if (!pe)
      break;
    ESP_LOGI(__FUNCTION__, "d_name=%s d_ino=%d d_type=%x", pe->d_name, pe->d_ino, pe->d_type);
  }
  closedir(dir);
}

int _draw_temperature(const measure_t& result, FontxFile *fx1, FontxFile *fx2, int x, int y, uint16_t color) {
  double temp_val = ((int)(result.value * 100 + .5) / 100.0);
  const static int len = 16;
  char tempBuf[len];

  // Integral part of temperature, in larger font
  if (result.fault == RTD_NoError) {
    sprintf(tempBuf, "%3d", (int)temp_val);
  } else {
    sprintf(tempBuf, "%s", TEMP_ERROR_STR);
  }
  int char_width = 32;

  // Draw integral part
  lcdDrawString(&dev, fx1, x, y, (uint8_t *)tempBuf, color);
  int width = strlen(tempBuf) * char_width;

  // Draw floating point now, as '.x'
  if (result.fault == RTD_NoError) {
    int point = static_cast<int>(temp_val * 10 - static_cast<int>(temp_val) * 10);
    sprintf(tempBuf, ".%d", point);
  } else {
    sprintf(tempBuf, ".-");
  }
  lcdDrawString(&dev, fx2, x + width, y - 3, (uint8_t *)tempBuf, color);
  return x + width + char_width + 5;
}

void _draw_setpoint(double setpoint, FontxFile *fx1, int x, int y, uint16_t color) {
  const static int len = 16;
  char tempBuf[len];
  sprintf(tempBuf, "/%.1f", setpoint);
  lcdDrawString(&dev, fx1, x, y, (uint8_t *)tempBuf, color);
}

void _draw_duty(int duty, FontxFile *fx1, int x, int y, uint16_t color) {
  const static int len = 16;
  char tempBuf[len];
  sprintf(tempBuf, "%3d%%", duty);
  lcdDrawString(&dev, fx1, x, y, (uint8_t *)tempBuf, color);
}

void _display_info(FontxFile *fx1) {
  uint8_t x = 12;
  uint8_t y = 40;
  lcdDrawString(&dev, fx1, x, y, (uint8_t *)"RebelEspresso", GREEN);

  y += 32;
  lcdDrawString(&dev, fx1, x, y, (uint8_t *)("v" FIRMWARE_VERSION), WHITE);

  y += 32;
  lcdDrawString(&dev, fx1, x, y, (uint8_t *)thing_info_id(), WHITE);
}

void _ensure_power_state_ok() {
  // look at last event received
  bool on = s_on;
  if (s_last_on_state != on) {
    ESP_LOGI(TAG, "Display power state change: %s -> %s", s_last_on_state ? "ON" : "OFF", on ? "ON" : "OFF");
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
      ESP_LOGI(TAG, "lcdInit model=0x%04x w=%d h=%d", model, CONFIG_WIDTH, CONFIG_HEIGHT);
      lcdInit(&dev, model, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETY, CONFIG_OFFSETX);
      ESP_LOGI(TAG, "lcdDisplayOn + fill");
      lcdDisplayOn(&dev);
      lcdFillScreen(&dev, BLACK);
      lcdSetFontFill(&dev, BLACK);
      lcdSetBrightness(100);
      lcdBacklightOn(&dev);
    } else {
      lcdDisplayOff(&dev);
      lcdBacklightOff(&dev);
    }
    s_last_on_state = on;
  }
}

static inline void _draw_pixel_safe(int x, int y, uint16_t color) {
  if (x < 0 || y < 0 || x >= CONFIG_WIDTH || y >= CONFIG_HEIGHT) {
    return;
  }
  lcdDrawPixel(&dev, static_cast<uint16_t>(x), static_cast<uint16_t>(y), color);
}

static void _draw_wifi_arc(int cx, int cy, int radius, uint16_t color) {
  for (int dx = -radius; dx <= radius; ++dx) {
    int inside = radius * radius - dx * dx;
    if (inside < 0) {
      continue;
    }
    int dy = static_cast<int>(roundf(sqrtf(static_cast<float>(inside))));
    _draw_pixel_safe(cx + dx, cy - dy, color);
  }
}

static void _draw_wifi_status_icon(bool connected, bool force_redraw) {
  int current_state = connected ? 1 : 0;
  if (!force_redraw && s_last_wifi_icon_state == current_state) {
    return;
  }

  const int x1 = CONFIG_WIDTH - 23;
  const int y1 = 0;
  const int x2 = CONFIG_WIDTH - 1;
  const int y2 = 18;
  lcdDrawFillRect(&dev, x1, y1, x2, y2, BLACK);
  s_last_wifi_icon_state = current_state;

  if (!connected) {
    return;
  }

  const int cx = CONFIG_WIDTH - 11;
  const int cy = 15;
  lcdDrawFillCircle(&dev, static_cast<uint16_t>(cx), static_cast<uint16_t>(cy), 2, WHITE);
  _draw_wifi_arc(cx, cy, 4, WHITE);
  _draw_wifi_arc(cx, cy, 7, WHITE);
  _draw_wifi_arc(cx, cy, 10, WHITE);
}

static void _draw_mqtt_status_icon(bool connected, bool force_redraw) {
  int current_state = connected ? 1 : 0;
  if (!force_redraw && s_last_mqtt_icon_state == current_state) {
    return;
  }

  const int x1 = CONFIG_WIDTH - 43;
  const int y1 = 0;
  const int x2 = CONFIG_WIDTH - 24;
  const int y2 = 18;
  lcdDrawFillRect(&dev, x1, y1, x2, y2, BLACK);
  s_last_mqtt_icon_state = current_state;

  if (!connected) {
    return;
  }

  // Small broker/network glyph: one hub with two upstream nodes.
  const int hub_x = CONFIG_WIDTH - 34;
  const int hub_y = 13;
  const int left_x = CONFIG_WIDTH - 39;
  const int top_y = 5;
  const int right_x = CONFIG_WIDTH - 29;

  lcdDrawLine(&dev, static_cast<uint16_t>(hub_x), static_cast<uint16_t>(hub_y), static_cast<uint16_t>(left_x),
              static_cast<uint16_t>(top_y), WHITE);
  lcdDrawLine(&dev, static_cast<uint16_t>(hub_x), static_cast<uint16_t>(hub_y), static_cast<uint16_t>(right_x),
              static_cast<uint16_t>(top_y), WHITE);
  lcdDrawFillCircle(&dev, static_cast<uint16_t>(hub_x), static_cast<uint16_t>(hub_y), 2, WHITE);
  lcdDrawFillCircle(&dev, static_cast<uint16_t>(left_x), static_cast<uint16_t>(top_y), 2, WHITE);
  lcdDrawFillCircle(&dev, static_cast<uint16_t>(right_x), static_cast<uint16_t>(top_y), 2, WHITE);
}

static void _draw_homekit_status_icon(bool connected, bool force_redraw) {
  int current_state = connected ? 1 : 0;
  if (!force_redraw && s_last_homekit_icon_state == current_state) {
    return;
  }

  const int x1 = CONFIG_WIDTH - 63;
  const int y1 = 0;
  const int x2 = CONFIG_WIDTH - 44;
  const int y2 = 18;
  lcdDrawFillRect(&dev, x1, y1, x2, y2, BLACK);
  s_last_homekit_icon_state = current_state;

  if (!connected) {
    return;
  }

  // Simple HomeKit-like house: roof + body + door.
  lcdDrawLine(&dev, static_cast<uint16_t>(CONFIG_WIDTH - 61), 10, static_cast<uint16_t>(CONFIG_WIDTH - 54), 4, WHITE);
  lcdDrawLine(&dev, static_cast<uint16_t>(CONFIG_WIDTH - 54), 4, static_cast<uint16_t>(CONFIG_WIDTH - 47), 10, WHITE);
  lcdDrawRect(&dev, static_cast<uint16_t>(CONFIG_WIDTH - 59), 10, static_cast<uint16_t>(CONFIG_WIDTH - 49), 16, WHITE);
  lcdDrawLine(&dev, static_cast<uint16_t>(CONFIG_WIDTH - 54), 16, static_cast<uint16_t>(CONFIG_WIDTH - 54), 12, WHITE);
}

static void _draw_active(FontxFile *fx0, FontxFile *fx16M, FontxFile *fx32M) {
  TickType_t startTick = xTaskGetTickCount();
  static bool show_circle = true;
  measure_t result = {};

  int header_width = 26;

  if (show_circle) {
    lcdDrawFillCircle(&dev, 5, 10, 5, GREEN);
  } else {
    lcdDrawFillCircle(&dev, 5, 10, 5, BLACK);
  }
  show_circle = !show_circle;

  _draw_duty(boiler_temp_get_duty(), fx0, 20, 24, WHITE);

  EventBits_t status_bits = xEventGroupGetBits(status_event_group);
  _draw_homekit_status_icon(homekit_has_active_connection(), s_force_status_icon_redraw);
  _draw_wifi_status_icon((status_bits & WIFI_CONNECTED_BIT) != 0, s_force_status_icon_redraw);
  _draw_mqtt_status_icon(mqtt_ha_is_connected(), s_force_status_icon_redraw);
  s_force_status_icon_redraw = false;

  // Header separator
  lcdDrawFillRect(&dev, 0, header_width, CONFIG_WIDTH - 1, header_width, GRAY);

  int x = 4;
  int y = 16;

  int vert_space = (240 - header_width) / 2;

  int voffset = 18;
  y += vert_space + 3 - voffset;
  rtds_get(&result, RTD_BREW_HEAD_IDX);
  // result.value = 88.3;
  // result.fault = RTD_NoError;
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
  lcdDrawString(&dev, fx0, last_pos + 20, y - 38, (uint8_t *)"Brew", GRAY);
  lcdDrawFillRect(&dev, 0, y + 2 + voffset, CONFIG_WIDTH - 1, y + 2 + voffset, GRAY);

  y += vert_space;
  static bool boiler_error_message = false;
  if (boiler_refill_state() == REFILL_STATE_ERROR) {
    if (!boiler_error_message) {
      lcdDrawFillRect(&dev, 0, y - vert_space + 24, CONFIG_WIDTH - 1, y + 16, BLACK);
    }
    boiler_error_message = true;
    lcdDrawString(&dev, fx16M, x + 20, y - 16, (uint8_t *)"Refill Error", RED);
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
    lcdDrawString(&dev, fx0, last_pos + 20, y - 38, (uint8_t *)"Boiler", GRAY);
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
  ESP_LOGD(TAG, "Render Took %" PRIu32 "ms\r\n", pdTICKS_TO_MS(endTick - startTick));
}

static void _draw_ap_mode(FontxFile *fx16M, int remaining_seconds) {
  int x = 20;
  int y = 25;

  lcdDrawString(&dev, fx16M, x, y, (uint8_t *)"WiFi Setup Mode", WHITE);

  wifi_ap_info_t ap_info = wifi_ap_get_info();
  lcdDrawString(&dev, fx16M, x, y + 32, (uint8_t *)"SSID:", WHITE);

  // Guard against right-edge artifacts by clipping to the available line width.
  char ssid_value_buf[24];
  constexpr size_t kMaxShownSsidChars = 17;
  size_t ssid_len = strnlen(ap_info.ssid, sizeof(ap_info.ssid));
  if (ssid_len > kMaxShownSsidChars) {
    snprintf(ssid_value_buf, sizeof(ssid_value_buf), "%.14s...", ap_info.ssid);
  } else {
    size_t copy_len = ssid_len;
    if (copy_len > (sizeof(ssid_value_buf) - 1)) {
      copy_len = sizeof(ssid_value_buf) - 1;
    }
    memcpy(ssid_value_buf, ap_info.ssid, copy_len);
    ssid_value_buf[copy_len] = '\0';
  }
  lcdDrawString(&dev, fx16M, x, y + 56, (uint8_t *)ssid_value_buf, GREEN);

  lcdDrawString(&dev, fx16M, x, y + 84, (uint8_t *)"Password:", WHITE);
  char setup_password[16] = {};
  if (web_auth_get_ap_setup_password(setup_password, sizeof(setup_password))) {
    lcdDrawString(&dev, fx16M, x, y + 108, (uint8_t *)setup_password, GREEN);
  } else {
    lcdDrawString(&dev, fx16M, x, y + 108, (uint8_t *)"(loading)", GRAY);
  }

  lcdDrawString(&dev, fx16M, x, y + 136, (uint8_t *)"Connect and open", WHITE);
  lcdDrawString(&dev, fx16M, x, y + 160, (uint8_t *)"192.168.4.1:8080", WHITE);

  // Keep the footer row clean before writing the changing countdown value.
  lcdDrawFillRect(&dev, 0, CONFIG_HEIGHT - 30, CONFIG_WIDTH - 1, CONFIG_HEIGHT - 1, BLACK);

  char countdown_buf[32];
  int minutes = remaining_seconds / 60;
  int seconds = remaining_seconds % 60;
  snprintf(countdown_buf, sizeof(countdown_buf), "Hides in %d:%02d", minutes, seconds);
  lcdDrawString(&dev, fx16M, 20, CONFIG_HEIGHT - 24, (uint8_t *)countdown_buf, GRAY);
}

static void _draw_descale_mode(FontxFile *fx) {
  int x = 36;
  int y = 108 + 24;

  lcdDrawString(&dev, fx, x, y, (uint8_t *)"Descaling Mode", WHITE);
}

static void _draw_refill_water_tank(FontxFile *fx) {
  int x = 32;
  int y = 108 + 24;

  lcdDrawString(&dev, fx, x, y, (uint8_t *)"Refill Tank", RED);
}

static void _draw_probe_service(FontxFile *fx) {
  int x = 12;
  int y = 108 + 24;

  lcdDrawString(&dev, fx, x, y, (uint8_t *)"Service Water Probe", RED);
}

static void _draw_brew_counter(FontxFile *fx1, FontxFile *fx2) {
  char buf[80];
  int seconds = (int)(pdTICKS_TO_MS(xTaskGetTickCount()) / 1000 - s_brew_start_time);
  sprintf(buf, "%ds", seconds);

  int num_chars = 2;
  if (seconds >= 10) {
    num_chars += 1;
  }
  if (seconds >= 100) {
    num_chars += 1;
  }

  int x = (CONFIG_WIDTH - num_chars * 32) / 2;
  int y = 84 + 24;

  uint16_t color;
  if (seconds > 45) {
    color = RED;
  } else if (seconds > 30) {
    color = PURPLE;
  } else {
    color = GREEN;
  }
  lcdDrawString(&dev, fx2, x, y, (uint8_t *)buf, color);

  // Brew counter
  auto status = brew_get_status();
  x = 5;
  y += 52;
  sprintf(buf, "Brew count: %lu", status.brew_count);
  lcdDrawString(&dev, fx1, x, y, (uint8_t *)buf, WHITE);
  y += 25;
  lcdDrawString(&dev, fx1, x, y, (uint8_t *)"Descale:", WHITE);
  y += 25;
  sprintf(buf, " -Count: %lu", status.descale_count);
  lcdDrawString(&dev, fx1, x, y, (uint8_t *)buf, WHITE);
  strftime(buf, sizeof(buf), " -Last: %d/%m/%y", localtime(&status.last_descale_time));
  y += 26;
  lcdDrawString(&dev, fx1, x, y, (uint8_t *)buf, WHITE);
}

static void _tick(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
  if (id != TICK) {
    return;
  }

  static int splash_count = 3;
  if (--splash_count > 0) {
    // Display initial info splash screen
    bool prior_state = s_on;
    s_on = true;
    _ensure_power_state_ok();
    _display_info(fx24G);
    s_on = prior_state;
  } else {
    EventBits_t uxBits = xEventGroupWaitBits(
        status_event_group, WIFI_CONNECTED_BIT | DESCALE_MODE_BIT | WIFI_AP_ACTIVE_BIT, false, true, 0);
    _ensure_power_state_ok();

    int state;

    int wifi_setup_remaining_seconds = _wifi_setup_overlay_seconds_remaining();

    if ((WIFI_AP_ACTIVE_BIT & uxBits) && !wifi_manager_is_connected() && wifi_setup_remaining_seconds > 0) {
      state = 1;
      if (state != last_state) {
        lcdFillScreen(&dev, BLACK);
        last_state = state;
        s_force_status_icon_redraw = true;
      }
      _draw_ap_mode(fx24M, wifi_setup_remaining_seconds);
    } else if (DESCALE_MODE_BIT & uxBits) {
      state = 2;
      if (state != last_state) {
        lcdFillScreen(&dev, BLACK);
        last_state = state;
        s_force_status_icon_redraw = true;
      }
      _draw_descale_mode(fx24M);
    } else if (process_image_get()->corrosion_status == CORROSION_FAULT) {
      state = 6;
      if (state != last_state) {
        lcdFillScreen(&dev, BLACK);
        last_state = state;
        s_force_status_icon_redraw = true;
      }
      _draw_probe_service(fx24M);
    } else if (hw_specs_is_aux_in_activated()) {
      state = 3;
      if (state != last_state) {
        lcdFillScreen(&dev, BLACK);
        last_state = state;
        s_force_status_icon_redraw = true;
      }
      _draw_refill_water_tank(fx32M);
    } else if (s_brew_start_time >= 0) {
      state = 4;
      if (state != last_state) {
        lcdFillScreen(&dev, BLACK);
        last_state = state;
        s_force_status_icon_redraw = true;
      }
      _draw_brew_counter(fx24M, fx64M);
    } else if (power_is_active()) {
      state = 5;
      if (state != last_state) {
        lcdFillScreen(&dev, BLACK);
        last_state = state;
        s_force_status_icon_redraw = true;
      }
      _draw_active(fx24M, fx32M, fx64M);
    }
  }
}

static void _power_events(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
  if (id == POWER_STANDBY) {
    s_on = false;
    _tick(nullptr, MACHINE_EVENTS, TICK, nullptr);
  } else if (id == POWER_ACTIVE) {
    s_on = true;
    s_brew_start_time = -1;
    _tick(nullptr, MACHINE_EVENTS, TICK, nullptr);
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

void display_init() {
  ESP_LOGI(TAG, "Initializing SPIFFS");

  esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs", .partition_label = NULL, .max_files = 5, .format_if_mount_failed = true};

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

  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, POWER_STANDBY, _power_events, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, POWER_ACTIVE, _power_events, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, BREW_STARTED, _brew_events, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, BREW_STOPPED, _brew_events, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(MACHINE_EVENTS, TICK, _tick, nullptr));

  spi_master_init(&dev, PIN_MOSI, PIN_SCK, PIN_OUT_DISPLAY_CS, PIN_OUT_DISPLAY_DC, PIN_OUT_DISPLAY_RESET,
                  PIN_OUT_DISPLAY_LED);

  InitFontx(fx16G, "/spiffs/ILGH16XB.FNT", ""); // 8x16Dot Gothic
  InitFontx(fx24G, "/spiffs/ILGH24XB.FNT", ""); // 12x24Dot Gothic
  InitFontx(fx32G, "/spiffs/ILGH32XB.FNT", ""); // 16x32Dot Gothic

  // set font file
  InitFontx(fx16M, "/spiffs/ILMH16XB.FNT", ""); // 8x16Dot Mincyo
  InitFontx(fx24M, "/spiffs/ILMH24XB.FNT", ""); // 12x24Dot Mincyo
  InitFontx(fx32M, "/spiffs/ILMH32XB.FNT", ""); // 16x32Dot Mincyo
  InitFontx(fx64M, "/spiffs/ILMH64XB.FNT", ""); // 32x64Dot Mincyo

  s_wifi_setup_overlay_deadline_us = esp_timer_get_time() + WIFI_SETUP_OVERLAY_DURATION_US;

  ESP_LOGI(TAG, "########### TFT READY ###########");
}