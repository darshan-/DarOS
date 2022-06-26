#include <stdint.h>

#include "rtc.h"
#include "rtc_int.h"

#include "interrupt.h"
#include "io.h"

#define REG_SEL 0x70
#define IO 0x71

#define SEC 0
#define MIN 0x02
#define HR 0x04
#define WKD 0x06
#define DAY 0x07
#define MTH 0x08
#define YR 0x09
#define SRA 0x0a
#define SRB 0x0b
#define SRC 0x0c
#define CEN 0x32

#define NMI_DISABLED (1<<7)
#define SRA_UPDATING (1<<7)

#define IRQF (1<<7)
#define INT_PERIODIC (1<<6)
#define INT_ALARM (1<<5)
#define INT_UPDATED (1<<4)

#define PM_BIT (1<<7)

#define HRS24 (1<<1)
#define BCD_OFF (1<<2)

uint64_t ms_since_boot = 0;
static uint64_t rtc_seconds, seconds_at_boot;

#define READ(r) outb(REG_SEL, r | NMI_DISABLED); \
    *q++ = inb(IO)

static void reenable_nmi() {
    outb(REG_SEL, SRB); // Doesn't matter what register we select, just that NMI_DISABLED isn't set.
    inb(IO);            // And doesn't matter what we read, just *that* we read after selecting.
}

static uint8_t* read(uint8_t* p) {
    uint8_t* q = p;

    outb(REG_SEL, SRA | NMI_DISABLED);
    while (inb(IO) & SRA_UPDATING)
        ;

    READ(SEC);
    READ(MIN);
    READ(HR);
    READ(WKD);
    READ(DAY);
    READ(MTH);
    READ(YR);
    READ(CEN);
    READ(SRB);

    reenable_nmi();

    return p;
}

static int equal(uint8_t* a, uint8_t* b) {
    for (int i = 0; i < 9; i++)
        if (a[i] != b[i])
            return 0;

    return 1;
}

// Assumes interrupts are off and leaves them off.  Call either before interrupts are first enabled at boot, or
//  from a helper function that knows what it's doing and turns them off before calling and back on afterward.
static void sync_rtc_seconds() {
    struct rtc_time t;
    uint8_t pm = 0;
    uint8_t a[9], b[9];

    while (!equal(read(a), read(b)))
        ;

    uint8_t* c = (uint8_t*) &t;
    for (int i = 0; i < 8; i++)
        c[i] = a[i];

    if (!(a[8] & HRS24)) {
        pm = t.hours & PM_BIT;
        t.hours ^= PM_BIT;
    }

    if (!(a[8] & BCD_OFF))
        for (int i = 0; i < 8; i++)
            c[i] = (c[i] >> 4) * 10 + (c[i] & 0x0f);

    if (pm)
        t.hours += 12;
    if (t.hours == 24) // Midnight
        t.hours = 0;

    // Now calculate seconds into day
    rtc_seconds = (t.hours * 60 + t.minutes) * 60 + t.seconds;

    // TODO: Let's also store rest of data.
}

// To be called only from IRQ8 handler; assumes interrupts are disabled (doesn't sti at end)
uint8_t irq8_type() {
    outb(REG_SEL, SRC | NMI_DISABLED);
    uint8_t c = inb(IO);

    reenable_nmi();

    if (!(c & IRQF))
        return RTC_INT_UNKNOWN;

    if (c & INT_PERIODIC)
        return RTC_INT_PERIODIC;
    if (c & INT_PERIODIC)
        return RTC_INT_PERIODIC;
    if (c & INT_PERIODIC)
        return RTC_INT_PERIODIC;

    return RTC_INT_UNKNOWN;
}

// To be called only when interrupts are safe to enable.
void get_rtc_time(struct rtc_time* t) {
    // Rather than essentially flooring cmos time, let's estimate that we're in the middle of a second (so add 500 ms)
    rtc_seconds = (seconds_at_boot + (ms_since_boot + 500) / 1000) % (60 * 60 * 24);
    // static uint64_t last_sync = 0;

    // TODO: I think I may still want this periodically?
    //       I'll also want to check RTC for day and date and stuff after day changes, at least until such
    //       a time as I do my own handling of months and leap things and whatnot.
    // if (rtc_seconds - last_sync > 10) {
    //     __asm__ __volatile__("cli");
    //     sync_rtc_seconds();
    //     last_sync = rtc_seconds;
    //     __asm__ __volatile__("sti");
    // }

    t->ms = ms_since_boot % 1000;
    t->seconds = rtc_seconds % 60;
    t->minutes = rtc_seconds / 60 % 60;
    t->hours = rtc_seconds / 60 / 60;
}

// Hmm, well, my qemu rtc timer seems to tick at 1,000,000 / 1024 Hz -- around 976 Hz, not 1024 Hz as advertised...
// So it's about as consistent as PIC, just slow when treated as 1024 Hz.  That's definitly *not* what all the docs
// I find say, so I guess it's a qemu bug?  In any case, that's why I seemed to have a lot of skew.  And I'm happy to
// be using the PIT now.

// To be called only before interrupts are first turned on at boot.
// static void enable_rtc_timer() {
//     outb(REG_SEL, SRB | NMI_DISABLED);
//     uint8_t regb = inb(IO);
//     outb(REG_SEL, SRB | NMI_DISABLED);
//     outb(IO, regb | INT_PERIODIC);

//     outb(REG_SEL, SRC | NMI_DISABLED);
//     inb(IO);

//     reenable_nmi();
// }

// To be called only before interrupts are first turned on at boot.
void init_rtc() {
    sync_rtc_seconds();
    seconds_at_boot = rtc_seconds;
    //enable_rtc_timer();
}
