#pragma once

#include <Max31865.h>


struct rtds_cfg_t {
};

struct rtd_data_t {
    uint16_t rtd_val;
    Max31865Error fault;
};

int rtds_init(const rtds_cfg_t* cfg);

void rtds_read_1(const rtds_cfg_t* cfg, struct rtd_data_t* data);

void rtds_read_2(const rtds_cfg_t* cfg, struct rtd_data_t* data);

void rtds_read_3(const rtds_cfg_t* cfg, struct rtd_data_t* data);

void rtds_read_4(const rtds_cfg_t* cfg, struct rtd_data_t* data);

