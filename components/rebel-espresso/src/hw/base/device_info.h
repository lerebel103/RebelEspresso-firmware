#pragma once

#include <cstdint>


void device_info_send(char* buffer, size_t max_len);

bool device_info_update_required();

void device_info_init();

