#pragma once

#define RTC_INT_UNKNOWN 0
#define RTC_INT_PERIODIC 0
#define RTC_INT_ALARM 1
#define RTC_INT_UPDATED 2

extern uint64_t seconds_since_boot;

void init_rtc();
uint8_t irq8_type();
