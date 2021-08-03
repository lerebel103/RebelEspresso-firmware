#include <esp_log.h>
#include <cmath>
#include "pid.h"
#include "window.h"

#define KEY_PID_P "pid.P"
#define KEY_PID_I "pid.I"
#define KEY_PID_D "pid.D"
#define KEY_PID_I_RESET_SEC "pid.i_reset_sec"
#define KEY_PID_I_RESET_TEMP "pid.i_reset_tem"
#define KEY_PID_SETPOINT0 "pid.sp0"
#define KEY_PID_SETPOINT1 "pid.sp1"
#define KEY_PID_OVER_SETPOINT_PERC "pid.over_sp_per"
#define KEY_PID_MIN_DUTY_BAND "pid.min_d_band"

#define TAG "pid"

const double PID_P_DEFAULT = 7;
const double PID_I_DEFAULT = 0.5;
const double PID_D_DEFAULT = 170;
const int32_t PID_I_RESET_SEC_DEFAULT = 60;
const double PID_I_RESET_TEMP_DEFAULT = 5;
const double PID_SETPOINT0_DEFAULT = 105;
const double PID_SETPOINT1_DEFAULT = 140;
const double PID_OVER_SETPOINT_PERC_DEFAULT = 8;
const double PID_MIN_DUTY_BAND_DEFAULT = 4;

void pid_load_nvram(nvs_handle my_handle, pid_cfg_t &cfg) {

    nvram_store_get_u64(my_handle, KEY_PID_P, (uint64_t *) &cfg.P,
                        (void *) &PID_P_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_PID_I, (uint64_t *) &cfg.I,
                        (void *) &PID_I_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_PID_D, (uint64_t *) &cfg.D,
                        (void *) &PID_D_DEFAULT);
    nvram_store_get_i32(my_handle, KEY_PID_I_RESET_SEC, (int32_t *) &cfg.I_reset_sec,
                        (void *) &PID_I_RESET_SEC_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_PID_I_RESET_TEMP, (uint64_t *) &cfg.I_reset_temp,
                        (void *) &PID_I_RESET_TEMP_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_PID_SETPOINT0, (uint64_t *) &cfg.setpoints[0],
                        (void *) &PID_SETPOINT0_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_PID_SETPOINT1, (uint64_t *) &cfg.setpoints[1],
                        (void *) &PID_SETPOINT1_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_PID_OVER_SETPOINT_PERC, (uint64_t *) &cfg.over_setpoint_perc,
                        (void *) &PID_OVER_SETPOINT_PERC_DEFAULT);
    nvram_store_get_u64(my_handle, KEY_PID_MIN_DUTY_BAND, (uint64_t *) &cfg.min_duty_band,
                        (void *) &PID_MIN_DUTY_BAND_DEFAULT);

}

void pid_save_nvram(nvs_handle my_handle, pid_cfg_t &cfg) {

    nvram_store_set_u64(my_handle, KEY_PID_P, (uint64_t *) &cfg.P);
    nvram_store_set_u64(my_handle, KEY_PID_I, (uint64_t *) &cfg.I);
    nvram_store_set_u64(my_handle, KEY_PID_D, (uint64_t *) &cfg.D);
    nvram_store_set_i32(my_handle, KEY_PID_I_RESET_SEC, (int32_t *) &cfg.I_reset_sec);
    nvram_store_set_u64(my_handle, KEY_PID_I_RESET_TEMP, (uint64_t *) &cfg.I_reset_temp);
    nvram_store_set_u64(my_handle, KEY_PID_SETPOINT0, (uint64_t *) &cfg.setpoints[0]);
    nvram_store_set_u64(my_handle, KEY_PID_SETPOINT1, (uint64_t *) &cfg.setpoints[1]);
    nvram_store_set_u64(my_handle, KEY_PID_OVER_SETPOINT_PERC, (uint64_t *) &cfg.over_setpoint_perc);
    nvram_store_set_u64(my_handle, KEY_PID_MIN_DUTY_BAND, (uint64_t *) &cfg.min_duty_band);
}

void pid_save_setpoint(nvs_handle my_handle, pid_cfg_t &cfg) {
    if (cfg.active_setpoint == 0) {
        nvram_store_set_u64(my_handle, KEY_PID_SETPOINT0,
                            (uint64_t *) &cfg.setpoints[cfg.active_setpoint]);
    } else {
        nvram_store_set_u64(my_handle, KEY_PID_SETPOINT1,
                            (uint64_t *) &cfg.setpoints[cfg.active_setpoint]);
    }
}

void pid_update(pid_cfg_t &dest, const pid_cfg_t &src) {
    // validate all fields
    if (src.P >= 0 && src.P < 60) {
        dest.P = src.P;
    }
    if (src.I >= 0 && src.I < 100 ) {
        dest.I = src.I;
    }
    if (src.D >= 0 && src.D < 800) {
        dest.D = src.D;
    }
    if (src.I_reset_sec >= 0 && src.I_reset_sec < 60 * 10) {
        dest.I_reset_sec = src.I_reset_sec;
    }
    if (src.I_reset_temp >= 0 && src.I_reset_temp < 30) {
        dest.I_reset_temp = src.I_reset_temp;
    }
    if (src.setpoints[0] >= SETPOINT0_MIN && src.setpoints[0] <= SETPOINT0_MAX) {
        dest.setpoints[0] = src.setpoints[0];
    }
    if (src.setpoints[1] >= SETPOINT1_MIN && src.setpoints[1] <= SETPOINT1_MAX) {
        dest.setpoints[1] = src.setpoints[1];
    }
    if (src.over_setpoint_perc >= 0 && src.over_setpoint_perc < 40) {
        dest.over_setpoint_perc = src.over_setpoint_perc;
    }
    if (src.min_duty_band >= 0 && src.min_duty_band <= 25) {
        dest.min_duty_band = src.min_duty_band;
    }
}

void pid_reset(pid_struct_t &pid) {
    ESP_LOGD(TAG, "Resetting...");
    pid.last_time_us = 0;
    pid.last_pid_err = 0;

    window_reset(&pid.data_window);
    ESP_LOGD(TAG, "Reset done.");
}

void pid_init(pid_struct_t &pid) {
    pid_reset(pid);
    window_init(&pid.data_window);
}

pid_result_t pid_process(
        pid_struct_t &pid,
        pid_cfg_t &cfg,
        uint64_t time_us, const rtd_data_t &data) {

    pid_result_t result = {
            .duty = 0,
            .is_over_threshold = false
    };

    // If we've had a gap, reset the PID
    double deltaT = (double) (time_us - pid.last_time_us) / 1e6;
    if (deltaT >= 5) {
        pid_reset(pid);
    } else {
        double setpoint = cfg.setpoints[cfg.active_setpoint];

        // Accumulate
        window_accumulate(&pid.data_window, time_us, &data, setpoint, cfg.I_reset_sec * 1e3);

        // Get window statistics
        static window_data_t wdata = {};
        window_data(&pid.data_window, &wdata);

        // delta from set-point, e.g. our error
        double error = setpoint - data.temperature;

        // Safety. If we are over set temperature by threshold, cut off
        if (cfg.over_setpoint_perc != 0 && -error > cfg.over_setpoint_perc * setpoint / 100) {
            result.is_over_threshold = true;
        } else {
            double duty = 0;
            // Derivative part
            double derivative = wdata.derivative;

            // Calculate duty, start with P and D
            duty = (cfg.P * error) + (cfg.D * derivative);

            // Integral is added if we are below our delta error temp
            if (fabs(error) < cfg.I_reset_temp) {
                ESP_LOGD(TAG, "I=%f, value=%f", cfg.I, (cfg.I * wdata.error_integral));
                duty += (cfg.I * wdata.error_integral);

            } else {
                // Keep on resetting window in this case
                window_reset(&pid.data_window);
            }

            ESP_LOGI(TAG, "Calculated duty: %f, P=%f, I=%f, D=%f", duty, (cfg.P * error), (cfg.I * wdata.error_integral), (cfg.D * derivative));
            result.duty = duty;
            pid.last_pid_err = error;
        }
    }

    pid.last_time_us = time_us;
    return result;
}