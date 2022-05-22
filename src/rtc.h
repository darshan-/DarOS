#pragma once

#include <stdint.h>

struct rtc_time {
    uint8_t seconds : 8;
    uint8_t minutes : 8;
    uint8_t hours : 8;
    uint8_t weekday : 8;
    uint8_t day_of_month : 8;
    uint8_t month : 8;
    uint8_t year : 8;
    uint8_t century : 8;
};

void get_rtc_time(struct rtc_time* t);
