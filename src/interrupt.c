#include <stdarg.h>
#include <stdint.h>
#include "serial.h"
#include "io.h"
#include "keyboard.h"
#include "list.h"
#include "malloc.h"
#include "periodic_callback.h"
#include "periodic_callback_int.h"
#include "rtc_int.h"


// TODO: Keep thinking about what I want to do about sprinkling cli and sti all over the place.
//   It's iffy turning interrupts back on sometimes -- maybe they still need to be off because of
//     the caller.  Or maybe interrupts haven't been turned on yet at boot -- it feels wrong having
//     to be super aware of that.  Set up some kind of lock or semaphore system or something?
//     Pretty sure there's a better approach than what I'm doing so far.  (Mabye just an API, so
//     this file knows whether interrupts should be on yet, and can keep track of how many callers
//     have requested to disable interrupts.  If that number drops to zero and we otherwise want them
//     on, turn them on?

/*

  If IRQ was initiated by the secondary PIC, that means both are involved, and both need an ack.
  If it was initiated by the primary, just send ack to that one.

 */

/*

  According to section 8.9.3 "Interrupt Stack Frame" of teh AMD manual vol 2,

        If the gate descriptor is an interrupt gate, RFLAGS.IF is cleared to 0.
        If the gate descriptor is a trapgate, RFLAGS.IF is not modified.

 */

#define IDT 0
#define CODE_SEG 8 // 1 quadword past null descriptor; next segment would be 16, etc.
#define TYPE_TRAP 0b1111
#define TYPE_INT 0b1110

#define PIC_ACK 0x20

#define ICW1 1<<4
#define ICW1_ICW4_NEEDED 1

#define PIC_PRIMARY_CMD 0x20
#define PIC_PRIMARY_DATA 0x21
#define PIC_SECONDARY_CMD 0xa0
#define PIC_SECONDARY_DATA 0xa1

#define TICK_HZ 2000

uint64_t int_tick_hz = TICK_HZ;  // For periodic_callbacks.c to access.

#define PIT_CMD 0x43
#define PIT_CH0_DATA 0x40
#define PIT_CH2_DATA 0x42
#define PIT_CHAN_0 0
#define PIT_CHAN_2 (1<<7)
#define PIT_WORD_RW (0b11<<4) // Read or write low byte then high byte, rather than just one byte
#define PIT_PERIODIC (0b10<<1)
//#define PIT_COUNT 65536
#define PIT_FREQ 1193182
#define PIT_COUNT (PIT_FREQ / TICK_HZ)

#define KERNEL_STACK_TOP 0xfffff

// Need extra level of indirection to quote a macro value, due to a special rule around argument prescan.
#define QUOT(v) #v
#define QUOTE(v) QUOT(v)

struct interrupt_frame {
    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t sp;
    uint64_t ss;
};


//static struct list* workQueue = (struct list*) 0;

#define INIT_WQ_CAP 20

static void **wq_start, **wq_head, **wq_tail;
static uint64_t wq_cap = 0;


#define log com1_printf

// void log(char* fmt, ...) {
//     fmt = fmt;
// }

static uint64_t pitCount = 0;

#define INC_WQ_PT(p) (p)++; if ((p) == wq_start + wq_cap) (p) = wq_start;

static inline void* wq_pop() {
    __asm__ __volatile__("cli");

    if (wq_head == wq_tail) return 0;

    void* f = *wq_head;
    INC_WQ_PT(wq_head);

    __asm__ __volatile__("sti");

    return f;
}

// To be called only when interrupts are off
static inline void wq_push(void* f) {
    // TODO:
    // I can have a work queue item that runs periodically (say, every 10 seconds?) that checks capcity
    //   and usage, and doubles capacity if usage is greater than half of capacity.  So we'll make it extremely
    //   unlikely we'll have to increase capacity here.
    if (wq_tail + 1 == wq_head || (wq_tail + 1 == wq_start + wq_cap && wq_head == wq_start)) {
        wq_cap *= 2;
        uint64_t head_off = wq_head - wq_start;
        uint64_t tail_off = wq_tail - wq_start;
        wq_start = realloc(wq_start, wq_cap*sizeof(void*));

        wq_head = wq_start + head_off;
        wq_tail = wq_start + tail_off;
    }

    *wq_tail = f;
    INC_WQ_PT(wq_tail);
}

void waitloop() {
    for (;;) {
        void (*f)();

        while ((f = (void (*)()) wq_pop()))
            f();

        __asm__ __volatile__(
            "mov $"
            QUOTE(KERNEL_STACK_TOP)
            ", %rsp\n" // We'll never return anywhere or use anything currently on the stack, so reset it
            "sti\n"
            "hlt\n"
        );
    }
}

static void dumpFrame(struct interrupt_frame *frame) {
    log("ip: 0x%p016h    cs: 0x%p016h flags: 0x%p016h\n", frame->ip, frame->cs, frame->flags);
    log("sp: 0x%p016h    ss: 0x%p016h\n", frame->sp, frame->ss);
}

static inline void generic_trap_n(struct interrupt_frame *frame, int n) {
    log("Generic trap handler used for trap vector 0x%h\n", n);
    dumpFrame(frame);

    // In generic case, it's not safe to do anything but go to waitloop (well, that may well not be safe either;
    //   halting the machine completly is probably best, but for now I'd like to do it this way rather than
    //   that or risk jumping to IP.
    // That means we can ignore whether there is an error code on the stack, as waitloop clears stack anyway.
    // So I think this should be a fine generic trap handler to default to when a specific one isn't available.
    frame->ip = (uint64_t) waitloop;
}

static inline void generic_etrap_n(struct interrupt_frame *frame, uint64_t error_code, int n) {
    log("Generic trap handler used for trap vector 0x%h, with error on stack; error: 0x%p016h\n", n, error_code);
    dumpFrame(frame);
    frame->ip = (uint64_t) waitloop;
    log("waitloop?");
}


// Is there an easier/cleaner/more efficient way to do this?  Macros are weird, clunky, too powerful and too
//   limited at the same time...

#define SET_GTRAP_N(nn) set_handler(0x##nn, trap_handler_0x##nn, TYPE_INT);

#define TRAP_N(nn) static void __attribute__((interrupt)) trap_handler_0x##nn(struct interrupt_frame *frame) {\
    generic_trap_n(frame, 0x##nn);\
}

#define SET_GETRAP_N(nn) set_handler(0x##nn, etrap_handler_0x##nn, TYPE_INT);

#define ETRAP_N(nn) static void __attribute__((interrupt)) etrap_handler_0x##nn(struct interrupt_frame *frame, uint64_t ec) { \
    generic_etrap_n(frame, ec, 0x##nn);\
}

#define TRAP_HL(macro, h, l) macro(h##l)

#define TRAPS(macro, h) \
TRAP_HL(macro, h, 0)\
TRAP_HL(macro, h, 1)\
TRAP_HL(macro, h, 2)\
TRAP_HL(macro, h, 3)\
TRAP_HL(macro, h, 4)\
TRAP_HL(macro, h, 5)\
TRAP_HL(macro, h, 6)\
TRAP_HL(macro, h, 7)\
TRAP_HL(macro, h, 8)\
TRAP_HL(macro, h, 9)\
TRAP_HL(macro, h, a)\
TRAP_HL(macro, h, b)\
TRAP_HL(macro, h, c)\
TRAP_HL(macro, h, d)\
TRAP_HL(macro, h, e)\
TRAP_HL(macro, h, f)

TRAPS(TRAP_N, 0)
TRAPS(TRAP_N, 1)

//ETRAP_N(08)
ETRAP_N(0a)
ETRAP_N(0b)
ETRAP_N(0c)
ETRAP_N(0d)
ETRAP_N(0e)
ETRAP_N(11)
ETRAP_N(15)
ETRAP_N(1d)
ETRAP_N(1e)


static void init_pic() {
    // ICW1
    outb(PIC_PRIMARY_CMD, ICW1 | ICW1_ICW4_NEEDED);
    outb(PIC_SECONDARY_CMD, ICW1 | ICW1_ICW4_NEEDED);

    // ICW2
    outb(PIC_PRIMARY_DATA, 0x20);   // Map  primary  PIC to 0x20 - 0x27
    outb(PIC_SECONDARY_DATA, 0x28); // Map secondary PIC to 0x28 - 0x2f

    // ICW3
    outb(PIC_PRIMARY_DATA, 1<<2);   // Secondary is at IRQ 2
    outb(PIC_SECONDARY_DATA, 2);    // Secondary is at IRQ 2

    // ICW4
    outb(PIC_PRIMARY_DATA, 1);      // 8086/8088 mode
    outb(PIC_SECONDARY_DATA, 1);    // 8086/8088 mode

    // Mask interrupts as you see fit
    // outb(PIC_PRIMARY_DATA, 0xff);
    // outb(PIC_SECONDARY_DATA, 0xff);
}

// void unmask_pics() {
//     outb(PIC_PRIMARY_DATA, 0);
//     outb(PIC_SECONDARY_DATA, 0);
// }

static void __attribute__((interrupt)) default_interrupt_handler(struct interrupt_frame *frame) {
    log("Default interrupt handler\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) default_PIC_P_handler(struct interrupt_frame *frame) {
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    log("Default primary PIC interrupt handler\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) default_PIC_S_handler(struct interrupt_frame *frame) {
    outb(PIC_SECONDARY_CMD, PIC_ACK);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    log("Default secondary PIC interrupt handler\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) divide_by_zero_handler(struct interrupt_frame *frame) {
    log("Divide by zero handler\n");
    frame->ip = (uint64_t) waitloop;
    dumpFrame(frame);
}

static void __attribute__((interrupt)) double_fault_handler(struct interrupt_frame *frame, uint64_t error_code) {
    log("Double fault; error should be zero.  error: 0x%p016h\n", error_code);
    dumpFrame(frame);
}

static void __attribute__((interrupt)) irq1_kbd(struct interrupt_frame *) {
    uint8_t code = inb(0x60);
    outb(PIC_PRIMARY_CMD, PIC_ACK);
    for (int i = 1; i < 1000*1000*20; i++)
        ;
    keyScanned(code);
}

static void __attribute__((interrupt)) irq8_rtc(struct interrupt_frame *) {
    outb(PIC_SECONDARY_CMD, PIC_ACK);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    // uint8_t type = irq8_type();
    // if (type == RTC_INT_PERIODIC) {
    // }
}

static void __attribute__((interrupt)) irq0_pit(struct interrupt_frame *) {
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    pitCount++;

    ms_since_boot = pitCount * PIT_COUNT * 1000 / PIT_FREQ;

    if (!periodicCallbacks.pcs) return;

    for (uint64_t i = 0; i < periodicCallbacks.len; i++) {
        if (periodicCallbacks.pcs[i]->count == 0 || periodicCallbacks.pcs[i]->count > TICK_HZ) {
            com1_printf("halting!\n");
            __asm__ __volatile__ ("hlt");
        }

        if (pitCount % (TICK_HZ * periodicCallbacks.pcs[i]->period / periodicCallbacks.pcs[i]->count) == 0)
            wq_push(periodicCallbacks.pcs[i]->f);
    }
}

static void set_handler(uint64_t vec, void* handler, uint8_t type) {
    uint16_t* idt_entry = (uint16_t*) (IDT + 16 * vec);
    uint64_t offset = (uint64_t) handler;
    *idt_entry = (uint16_t) (offset & 0xffff);
    offset >>= 16;
    *(idt_entry+3) = (uint16_t) (offset & 0xffff);
    offset >>= 16;
    *((uint32_t*) (idt_entry+4)) = (uint32_t) offset;

    *(idt_entry+2) = (uint16_t) 1<<15 | type << 8;
    *(idt_entry+1) = CODE_SEG;
}

static void init_pit() {
    outb(PIT_CMD, PIT_CHAN_0 | PIT_WORD_RW | PIT_PERIODIC);
    outb(PIT_CH0_DATA, PIT_COUNT & 0xff);
    outb(PIT_CH0_DATA, PIT_COUNT >> 8);
}

static void print_wq_status() {
    com1_printf("Work queue now has capacity: %u\n", wq_cap);
}

void init_interrupts() {
    // for (int i = 0; i < 32; i++)
    //     set_handler(i, default_trap_handler, TYPE_INT);
    for (int i = 32; i < 40; i++)
        set_handler(i, default_PIC_P_handler, TYPE_INT);
    for (int i = 40; i < 48; i++)
        set_handler(i, default_PIC_S_handler, TYPE_INT);
    for (int i = 48; i < 256; i++)
        set_handler(i, default_interrupt_handler, TYPE_INT);

    set_handler(0x20, irq0_pit, TYPE_INT);
    set_handler(0x21, irq1_kbd, TYPE_INT);
    set_handler(0x28, irq8_rtc, TYPE_INT);

    TRAPS(SET_GTRAP_N, 0);
    TRAPS(SET_GTRAP_N, 1);

    set_handler(0, divide_by_zero_handler, TYPE_INT);

    // These are the traps with errors on stack according to https://wiki.osdev.org/Exceptions, 
    set_handler(8, double_fault_handler, TYPE_INT);
    //SET_GETRAP_N(08);
    SET_GETRAP_N(0a);
    SET_GETRAP_N(0b);
    SET_GETRAP_N(0c);
    SET_GETRAP_N(0d);
    SET_GETRAP_N(0e);
    SET_GETRAP_N(11);
    SET_GETRAP_N(15);
    SET_GETRAP_N(1d);
    SET_GETRAP_N(1e);

    init_rtc();
    init_pit();
    init_pic();

    wq_start = wq_head = wq_tail = malloc(INIT_WQ_CAP * sizeof(void*));
    wq_cap = INIT_WQ_CAP;

    registerPeriodicCallback((struct periodic_callback) {1, 2, print_wq_status});

    __asm__ __volatile__("sti");
}
