#pragma once

#include <freertos/FreeRTOS.h>
#include <stdint.h>

void wifi_init();
void wifi_terminate();

uint32_t wifi_get_error_count();

void wifi_tick(TickType_t tick);

const uint8_t* wifi_get_prov_qr();
int wifi_get_prov_qr_len();
