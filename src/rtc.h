#pragma once

#include <stdint.h>

#define CMOS_REG_SEL 0x70
#define CMOS_IO 0x71
#define RTC_SRA 0x0a
#define RTC_SRB 0x0b
#define RTC_SRC 0x0c

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
uint8_t read_rtc_reg(uint8_t reg);
void enable_rtc_timer();
