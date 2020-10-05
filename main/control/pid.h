#pragma once

struct pid_setpoint_t {
    double temp_min;
    double temp_max;
};

struct pid_cfg_t {
    float P = 2;
    float I = 1.6;
    float D = 800;

    int window_s = 15;
};