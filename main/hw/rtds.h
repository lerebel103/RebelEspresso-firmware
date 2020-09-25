#pragma once

#include <Max31865.h>


struct rtds_cfg_t {
};

struct rtd_data_t {
    double temperature;
    Max31865Error fault;
};

int rtds_init(const rtds_cfg_t* cfg);

void rtds_read_1(struct rtd_data_t* data);

void rtds_read_2(struct rtd_data_t* data);

void rtds_read_3(struct rtd_data_t* data);

void rtds_read_4(struct rtd_data_t* data);

