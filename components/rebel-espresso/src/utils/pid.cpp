#include "pid.h"
#include <cmath>
#include <esp_log.h>

#define KEY_PID_P "pid.P"
#define KEY_PID_I "pid.I"
#define KEY_PID_D "pid.D"
#define KEY_PID_I_RESET_TEMP "pid.i_reset_tem"
#define KEY_PID_SETPOINT0 "pid.sp0"
#define KEY_PID_SETPOINT1 "pid.sp1"
#define KEY_PID_OVER_SETPOINT_PERC "pid.over_sp_per"

#define TAG "pid"

const double PID_P_DEFAULT = 7;
const double PID_I_DEFAULT = 0.5;
const double PID_D_DEFAULT = 170;
const double PID_I_RESET_TEMP_DEFAULT = 5;
const double PID_SETPOINT0_DEFAULT = 105;
const double PID_SETPOINT1_DEFAULT = 140;
const double PID_OVER_SETPOINT_PERC_DEFAULT = 8;

#define INTEGRAL_MAX 25

void pid_load_nvram(nvs_handle my_handle, pid_cfg_t &cfg) {

  nvram_store_get_u64(my_handle, KEY_PID_P, (uint64_t *)&cfg.P,
                      (void *)&PID_P_DEFAULT);
  nvram_store_get_u64(my_handle, KEY_PID_I, (uint64_t *)&cfg.I,
                      (void *)&PID_I_DEFAULT);
  nvram_store_get_u64(my_handle, KEY_PID_D, (uint64_t *)&cfg.D,
                      (void *)&PID_D_DEFAULT);
  nvram_store_get_u64(my_handle, KEY_PID_I_RESET_TEMP,
                      (uint64_t *)&cfg.I_reset_temp,
                      (void *)&PID_I_RESET_TEMP_DEFAULT);
  nvram_store_get_u64(my_handle, KEY_PID_SETPOINT0,
                      (uint64_t *)&cfg.setpoints[0],
                      (void *)&PID_SETPOINT0_DEFAULT);
  nvram_store_get_u64(my_handle, KEY_PID_SETPOINT1,
                      (uint64_t *)&cfg.setpoints[1],
                      (void *)&PID_SETPOINT1_DEFAULT);
  nvram_store_get_u64(my_handle, KEY_PID_OVER_SETPOINT_PERC,
                      (uint64_t *)&cfg.over_setpoint_perc,
                      (void *)&PID_OVER_SETPOINT_PERC_DEFAULT);
}

void pid_save_nvram(nvs_handle my_handle, pid_cfg_t &cfg) {

  nvram_store_set_u64(my_handle, KEY_PID_P, (uint64_t *)&cfg.P);
  nvram_store_set_u64(my_handle, KEY_PID_I, (uint64_t *)&cfg.I);
  nvram_store_set_u64(my_handle, KEY_PID_D, (uint64_t *)&cfg.D);
  nvram_store_set_u64(my_handle, KEY_PID_I_RESET_TEMP,
                      (uint64_t *)&cfg.I_reset_temp);
  nvram_store_set_u64(my_handle, KEY_PID_SETPOINT0,
                      (uint64_t *)&cfg.setpoints[0]);
  nvram_store_set_u64(my_handle, KEY_PID_SETPOINT1,
                      (uint64_t *)&cfg.setpoints[1]);
  nvram_store_set_u64(my_handle, KEY_PID_OVER_SETPOINT_PERC,
                      (uint64_t *)&cfg.over_setpoint_perc);
}

void pid_save_setpoint(nvs_handle my_handle, pid_cfg_t &cfg) {
  if (cfg.active_setpoint == 0) {
    nvram_store_set_u64(my_handle, KEY_PID_SETPOINT0,
                        (uint64_t *)&cfg.setpoints[cfg.active_setpoint]);
  } else {
    nvram_store_set_u64(my_handle, KEY_PID_SETPOINT1,
                        (uint64_t *)&cfg.setpoints[cfg.active_setpoint]);
  }
}

void pid_update(pid_cfg_t &dest, const pid_cfg_t &src) {
  // validate all fields
  if (src.P >= 0 && src.P < 60) {
    dest.P = src.P;
  }
  if (src.I >= 0 && src.I < 100) {
    dest.I = src.I;
  }
  if (src.D >= 0 && src.D < 5000) {
    dest.D = src.D;
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
}

void pid_reset(pid_struct_t &pid) {
  ESP_LOGD(TAG, "Resetting...");

  pid.error = 0;
  pid.proportional = 0;
  pid.derivative = 0;
  pid.integral = 0;

  pid.last_time_us = 0;
  pid.last_data_value = 0;

  ESP_LOGD(TAG, "Reset done.");
}

void pid_init(pid_struct_t &pid) {
  pid_reset(pid);
}

pid_result_t pid_process(pid_struct_t &pid, pid_cfg_t &cfg, uint64_t time_us,
                         const reading_t &data) {

  pid_result_t result = {.duty = 0, .is_over_threshold = false};
  double deltaT = (double)(time_us - pid.last_time_us) / 1e6;

  if (deltaT >= 5) {
    // Suspicious, we just reset the PID if large gaps seen
    pid_reset(pid);
  } else if (pid.last_time_us != 0) {
    double setpoint = cfg.setpoints[cfg.active_setpoint];

    double error = setpoint - data.value;
    pid.error = error;
    pid.proportional = cfg.P * error;
    pid.integral += cfg.I * (error * deltaT);
    pid.derivative = cfg.D * (data.value - pid.last_data_value) / deltaT;

    // Integral is added if we are below our delta error temp
    if (fabs(error) > cfg.I_reset_temp) {
      pid.integral = 0;
    }

    // We also cap integral value always
    if (pid.integral > INTEGRAL_MAX) {
      pid.integral = INTEGRAL_MAX;
    } else if (pid.integral < -INTEGRAL_MAX) {
      pid.integral = -INTEGRAL_MAX;
    }

    // Calculate duty now
    result.duty = pid.proportional + pid.integral + pid.derivative;

    ESP_LOGI(TAG, "Calculated duty: %f, P=%f, I=%f, D=%f", result.duty,
             pid.proportional, pid.integral, pid.derivative);

    // Safety. If we are over set temperature by threshold, cut off
    if (cfg.over_setpoint_perc != 0 &&
        -error > cfg.over_setpoint_perc * setpoint / 100) {
      result.is_over_threshold = true;
    }
  }

  pid.last_time_us = time_us;
  pid.last_data_value = data.value;
  return result;
}