#pragma once

struct pid_cfg_t {
    /**
     * Target setpoint
     */
    float setpoint = 0;

    float P = 3.5;
    float I = 0.5;
    float D = 35;

    /**
     * Integral is trimmed to this many seconds always
     */
    int I_reset_s = 15;

    /**
     * Integral is discarded if delta temperature to setpoint is above this value
     */
    int I_reset_temp = 10;
};