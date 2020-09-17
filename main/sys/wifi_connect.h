#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>

void wifi_init();
void wifi_terminate();

void wifi_set_ssid(const char *ssid);
void wifi_set_password(const char *password);
void wifi_set_tx_power(int power);

uint32_t wifi_get_error_count();

void wifi_tick(TickType_t tick);
