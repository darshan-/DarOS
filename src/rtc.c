#include <stdint.h>
#include "interrupt.h"
#include "io.h"
#include "rtc.h"

#define RTC_SEC 0
#define RTC_MIN 0x02
#define RTC_HR 0x04
#define RTC_WKD 0x06
#define RTC_DAY 0x07
#define RTC_MTH 0x08
#define RTC_YR 0x09
#define RTC_CEN 0x32
#define RTC_SRA_UPDATING (1<<7)

#define PM_BIT (1<<7)

#define HRS24 (1<<1)
#define BCD_OFF (1<<2)

#define READ(r) outb(CMOS_REG_SEL, r); \
    *q++ = inb(CMOS_IO)

static uint8_t* read(uint8_t* p) {
    uint8_t* q = p;

    __asm__ __volatile__("cli");

    outb(CMOS_REG_SEL, RTC_SRA);
    while (inb(CMOS_IO) & RTC_SRA_UPDATING)
        ;

    READ(RTC_SEC);
    READ(RTC_MIN);
    READ(RTC_HR);
    READ(RTC_WKD);
    READ(RTC_DAY);
    READ(RTC_MTH);
    READ(RTC_YR);
    READ(RTC_CEN);
    READ(RTC_SRB);

    __asm__ __volatile__("sti");

    return p;
}

static int equal(uint8_t* a, uint8_t* b) {
    for (int i = 0; i < 9; i++)
        if (a[i] != b[i])
            return 0;

    return 1;
}

// Disables interrupts and then reenables them (to prevent potential issue with interrupt happening and
//  port 0x70 changing before we read from port 0x71), so must not be called until after interrupt
//  handlers are set up.
void read_rtc(struct rtc_time* t) {
    uint8_t pm = 0;
    uint8_t a[9], b[9];

    while (!equal(read(a), read(b)))
        ;

    uint8_t* c = (uint8_t*) t;
    for (int i = 0; i < 8; i++)
        c[i] = a[i];

    if (!(a[8] & HRS24)) {
        pm = t->hours & PM_BIT;
        t->hours ^= PM_BIT;
    }

    if (!(a[8] & BCD_OFF))
        for (int i = 0; i < 8; i++)
            c[i] = (c[i] >> 4) * 10 + (c[i] & 0x0f);

    if (pm)
        t->hours += 12;
    if (t->hours == 24) // Midnight
        t->hours = 0;
}
