#pragma once

#include <stdint.h>

#define RTC_INT_UNKNOWN 0
#define RTC_INT_PERIODIC 0
#define RTC_INT_ALARM 1
#define RTC_INT_UPDATED 2

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

void read_rtc(struct rtc_time* time);
uint8_t irq8_type();
void enable_rtc_timer();
