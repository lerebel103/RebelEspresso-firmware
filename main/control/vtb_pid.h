#pragma once

#include <ctime>

// Variable time base PID
void vtb_pid_init();

void vtb_pid_tick(int64_t time_us);
