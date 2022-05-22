#include <stdint.h>
#include "interrupt.h"
#include "io.h"
#include "rtc.h"
#include "rtc_int.h"

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

//static struct rtc_time
//static uint64_t seconds; // Seconds into the day; calculated from RTC when we read it, incremented on timer tick.
extern uint64_t rtc_seconds = 0;

#define READ(r) outb(REG_SEL, r | NMI_DISABLED); \
    *q++ = inb(IO)

static void reenable_nmi() {
    outb(REG_SEL, SRB); // Doesn't matter what register we select, just that NMI_DISABLED isn't set.
    inb(IO);            // And doesn't matter what we read, just *that* we read after selecting.
}

static uint8_t* read(uint8_t* p) {
    uint8_t* q = p;

    __asm__ __volatile__("cli");

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

void enable_rtc_timer() {
    __asm__ __volatile__("cli");

    outb(REG_SEL, SRB | NMI_DISABLED);
    uint8_t regb = inb(IO);
    outb(REG_SEL, SRB | NMI_DISABLED);
    outb(IO, regb | INT_PERIODIC);

    outb(REG_SEL, SRC | NMI_DISABLED);
    inb(IO);

    reenable_nmi();
    __asm__ __volatile__("sti");
}
