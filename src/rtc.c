#include <stdint.h>
#include "rtc.h"

/*
        RTC_SEC equ 0
        RTC_MIN equ 0x02
        RTC_HR equ 0x04
        RTC_WKD equ 0x06
        RTC_DAY equ 0x07
        RTC_MTH equ 0x08
        RTC_YR equ 0x09
        RTC_CEN equ 0x32
        RTC_STA equ 0x0a
        RTC_STB equ 0x0a
 */
// struct rtc {
//     uint8_t seconds : 8;
// };


/*
  Or just treat it as an array, since it kinda is, all 8-bit values, so we can just have constants for offset?
  rtc_data[SECONDS] vs rtc_data.seconds -- seems fine either way.
  Especially since we'll want to stardardize to something usable, this file will interpret raw data according
  to status register B, so we can have our own struct -- yeah, let's just use an array here for raw data, and
  export data in our own format.
 */


#define SECONDS 0
#define MINUTES 1
#define HOURS 2
#define WEEKDAY 3
#define DAY_OF_MONTH 4
#define MONTH 5
#define YEAR 6
#define CENTURY 7
#define STATUS_REG_B 8

#define HRS24 1<<1
#define BCD 1<<2

extern uint8_t (*RTC)[9];
#define rtc (*RTC)

//void get_rtc_data(struct rtc* data) {
void read_rtc(struct rtc_time* time) {
    uint8_t hrs24 = rtc[STATUS_REG_B] & HRS24;
    uint8_t bcd = rtc[STATUS_REG_B] & BCD;
    com1_printf("bcd: %u\n", bcd);

    if (bcd)
        for (int i = 0; i < 8; i++)
            rtc[i] = (rtc[i] >> 4) * 10 + (rtc[i] & 0x0f);

    time->seconds = rtc[SECONDS];
    time->minutes = rtc[MINUTES];
    time->hours = rtc[HOURS];
}
