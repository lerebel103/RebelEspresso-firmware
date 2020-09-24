#pragma once


#include <cstdint>
#include <cJSON.h>

#include <freertos/FreeRTOS.h>


void mqtt_init();

bool mqtt_send_status(const char* message);

void mqtt_set_project_id(const char *val);

void mqtt_set_location(const char *val);

void mqtt_set_registry_id(const char *val);

void mqtt_set_client_private_key(const char *val);

uint32_t mqtt_get_total_error_count();

TickType_t mqtt_last_connect_attempt();


void mqtt_set_ota_cfg_cb(void (*cb)(const cJSON*));
