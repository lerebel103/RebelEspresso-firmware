#include "sntp.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_attr.h>
#include <esp_sleep.h>
#include <nvs_flash.h>
#include <lwip/err.h>
#include <lwip/apps/sntp.h>

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>


#include "events.h"
#include "nvram_store.h"
#include "state.h"

static const char *SNTP_TAG = "SNTP";
#define MAX_TZ_LEN 64
char g_system_tz[MAX_TZ_LEN];

static TickType_t g_start_tick = 0;
static int g_sntp_sync_duration = -1;

static void apply_tz(const char *timezone) {
    ESP_LOGI(SNTP_TAG, "Setting POSIX timezone to %s", timezone);

    setenv("TZ", timezone, 1);
    tzset();
}

static void time_synced_handler(struct timeval *new_time) {
    xEventGroupSetBits(status_event_group, TIME_SYNC_BIT);

    time_t now;
    time(&now);
    char strftime_buf[80];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", localtime(&now));

    ESP_LOGI(SNTP_TAG, "Got time sync, current time is %s", strftime_buf);

    // This is only possible for the first call back, we won't know when the start is as NTP runs
    // on regular intervals later on (every hour), so cannot gauge how long subsequent syncs take
    if (g_sntp_sync_duration <= 0) {
        g_sntp_sync_duration = xTaskGetTickCount() * portTICK_PERIOD_MS - g_start_tick;
    }
}

void initialize_sntp(const char* primary_server) {
    g_start_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;
    sntp_stop();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    if (primary_server != nullptr) {
        ESP_LOGI(SNTP_TAG, "Initializing SNTP with primary server '%s'", primary_server);
        sntp_setservername(0, primary_server);
    } else {
        ESP_LOGI(SNTP_TAG, "Initializing SNTP with primary server 'time.google.com'");
        sntp_setservername(0, "time.google.com");
    }

    sntp_setservername(1, "0.pool.ntp.org");
    sntp_setservername(2, "1.pool.ntp.org");
    sntp_setservername(3, "2.pool.ntp.org");

    // Register CB so we know when time gets synchronised
    sntp_set_time_sync_notification_cb(&time_synced_handler);

    sntp_init();
}

void sntp_set_system_tz(const char *tz) {
    esp_err_t err = nvram_store_write_str("system_tz", tz);
    ESP_ERROR_CHECK(err);
    strcpy(g_system_tz, tz);
    apply_tz(tz);
}

const char *sntp_get_system_tz() {
    esp_err_t err = nvram_store_read_str("system_tz", g_system_tz, MAX_TZ_LEN, DEFAULT_SYSTEM_TZ);
    ESP_ERROR_CHECK(err);
    return g_system_tz;
}


void sntp_sync_init(const char* primary_server) {
    apply_tz(sntp_get_system_tz());
    initialize_sntp(primary_server);
}

int sntp_sync_get_sync_duration() {
    return g_sntp_sync_duration;
}

