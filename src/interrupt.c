#include <stdarg.h>
#include <stdint.h>

#include "interrupt.h"

#include "io.h"
#include "keyboard.h"
#include "list.h"
#include "log.h"
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


// TODO: Do I really want everything set up as INT rather than TRAP?  I like interrupts being turned off
//  for me automatically, but do I understand the implications of this change?  Am I really free to just
//  choose as a I please, whether a given interrupt vector is tagged as interrupt or trap?

/*

  If IRQ was initiated by the secondary PIC, that means both are involved, and both need an ack.
  If it was initiated by the primary, just send ack to that one.

 */

#define IDT 0
#define CODE_SEG 8 // 1 quadword past null descriptor; next segment would be 16, etc. TODO: Grab from CS?
#define TYPE_TRAP 0b1111
#define TYPE_INT 0b1110

#define PIC_ACK 0x20

#define ICW1 1<<4
#define ICW1_ICW4_NEEDED 1

#define PIC_PRIMARY_CMD 0x20
#define PIC_PRIMARY_DATA 0x21
#define PIC_SECONDARY_CMD 0xa0
#define PIC_SECONDARY_DATA 0xa1

#define TICK_HZ 1000

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

uint64_t* kernel_stack_top;

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


#define INIT_WQ_CAP 20
#define INIT_KB_CAP 20

uint64_t int_blocks = 0;

uint64_t read_tsc() {
    uint64_t tsc;

    // edx gets high-order doubleword, eax gets low-order doubleword

    __asm__ __volatile__(
        "cpuid\n"
        "rdtsc\n"
        "shl $32, %%rdx\n"
        "add %%rdx, %%rax\n"
        "mov %%rax, %0\n"
        :"=m"(tsc)
    );

    return tsc;
}

static uint64_t pitCount = 0;

struct queue {
    void** start;
    void** head;
    void** tail;
    uint64_t cap;
};

static struct queue wq = {};
// It's quite space inefficient to have 8-byte wide slots for key codes, but I don't expect the buffer to
//   ever get very full, and it's simplest to just have one queue type, I think?
static struct queue kbd_buf = {};


#define INITQ(q, c) q.start = q.head = q.tail = malloc(c * sizeof(void*)); \
    q.cap = c

#define INC_Q_PT(q, p) (q->p)++; if ((q->p) == q->start + q->cap) (q->p) = q->start;

static inline void* pop(struct queue* q) {
    __asm__ __volatile__("cli"); // I think I want to special case this and not use no_ints() and ints_okay()?

    if (q->head == q->tail) return 0;

    void* p = *(q->head);
    INC_Q_PT(q, head);

    __asm__ __volatile__("sti");

    return p;
}

// To be called only when interrupts are off (From handler or during initialization);
static inline void push(struct queue* q, void* p) {
    if (q->tail + 1 == q->head || (q->tail + 1 == q->start + q->cap && q->head == q->start)) {
        q->cap *= 2;
        uint64_t head_off = q->head - q->start;
        uint64_t tail_off = q->tail - q->start;
        q->start = realloc(q->start, q->cap * sizeof(void*));

        q->head = q->start + head_off;
        q->tail = q->start + tail_off;
    }

    *(q->tail) = p;
    INC_Q_PT(q, tail);
}

void waitloop() {
    for (;;) {
        void (*f)();

        while ((f = (void (*)()) pop(&wq)))
            f();

        __asm__ __volatile__(
            "mov %0, %%rsp\n" // We'll never return anywhere or use anything currently on the stack, so reset it
            "sti\n"
            "hlt\n"
            ::"m"(kernel_stack_top)
        );
    }
}

void process_keys() {
    uint8_t code = (uint8_t) (uint64_t) pop(&kbd_buf);
    keyScanned(code);
}

static void dumpFrame(struct interrupt_frame *frame) {
    logf("ip: 0x%p016h    cs: 0x%p016h flags: 0x%p016h\n", frame->ip, frame->cs, frame->flags);
    logf("sp: 0x%p016h    ss: 0x%p016h\n", frame->sp, frame->ss);
}

static inline void generic_trap_n(struct interrupt_frame *frame, int n) {
    logf("Generic trap handler used for trap vector 0x%h\n", n);
    dumpFrame(frame);

    // In generic case, it's not safe to do anything but go to waitloop (well, that may well not be safe either;
    //   halting the machine completly is probably best, but for now I'd like to do it this way rather than
    //   that or risk jumping to IP.
    // That means we can ignore whether there is an error code on the stack, as waitloop clears stack anyway.
    // So I think this should be a fine generic trap handler to default to when a specific one isn't available.
    frame->ip = (uint64_t) waitloop;
}

static inline void generic_etrap_n(struct interrupt_frame *frame, uint64_t error_code, int n) {
    logf("Generic trap handler used for trap vector 0x%h, with error on stack; error: 0x%p016h\n", n, error_code);
    dumpFrame(frame);
    frame->ip = (uint64_t) waitloop;
    logf("waitloop?");
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
//ETRAP_N(0e)
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
    outb(PIC_PRIMARY_DATA, 0x7c);
    outb(PIC_SECONDARY_DATA, 0xff);
}

// void unmask_pics() {
//     outb(PIC_PRIMARY_DATA, 0);
//     outb(PIC_SECONDARY_DATA, 0);
// }

static void __attribute__((interrupt)) default_interrupt_handler(struct interrupt_frame *frame) {
    logf("Default interrupt handler\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) default_PIC_P_handler(struct interrupt_frame *frame) {
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    logf("Default primary PIC interrupt handler\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) default_PIC_S_handler(struct interrupt_frame *frame) {
    outb(PIC_SECONDARY_CMD, PIC_ACK);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    logf("Default secondary PIC interrupt handler\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) divide_by_zero_handler(struct interrupt_frame *frame) {
    logf("Divide by zero handler\n");
    frame->ip = (uint64_t) waitloop;
    dumpFrame(frame);
}

static void __attribute__((interrupt)) trap_0x0e_page_fault(struct interrupt_frame *frame, uint64_t error_code) {
    logf("Page fault; error: 0x%p016h\n", error_code);
    dumpFrame(frame);

    uint64_t cr2;

    __asm__ __volatile__(
        "mov %%cr2, %%rax\n"
        "mov %%rax, %0\n"
        :"=m"(cr2)
    );

    logf("cr2: %p016h\n", cr2);

    frame->ip = (uint64_t) waitloop;
}

static void __attribute__((interrupt)) double_fault_handler(struct interrupt_frame *frame, uint64_t error_code) {
    logf("Double fault; error should be zero.  error: 0x%p016h\n", error_code);
    dumpFrame(frame);
}

static void __attribute__((interrupt)) irq1_kbd(struct interrupt_frame *) {
    uint8_t code = inb(0x60);
    outb(PIC_PRIMARY_CMD, PIC_ACK);
    push(&kbd_buf, (void*) (uint64_t) code);
    push(&wq, process_keys);
}

static void __attribute__((interrupt)) irq8_rtc(struct interrupt_frame *) {
    outb(PIC_SECONDARY_CMD, PIC_ACK);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    // uint8_t type = irq8_type();
    // if (type == RTC_INT_PERIODIC) {
    // }
}

static uint64_t cpuCountOffset = 0;

static void __attribute__((interrupt)) irq0_pit(struct interrupt_frame *) {
    //logf("0");
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    // if (cpuCountOffset == 0)
    //     cpuCountOffset = read_tsc();

    pitCount++;

    ms_since_boot = pitCount * PIT_COUNT * 1000 / PIT_FREQ;

    // if (pitCount % TICK_HZ == 0)
    //     logf("Average CPU ticks per PIT tick: %u\n", (read_tsc() - cpuCountOffset) / pitCount);

    if (!periodicCallbacks.pcs) return;

    for (uint64_t i = 0; i < periodicCallbacks.len; i++) {
        if (periodicCallbacks.pcs[i]->count == 0 || periodicCallbacks.pcs[i]->count > TICK_HZ) {
            logf("halting!\n");
            __asm__ __volatile__ ("hlt");
        }

        if (pitCount % (TICK_HZ * periodicCallbacks.pcs[i]->period / periodicCallbacks.pcs[i]->count) == 0)
            push(&wq, periodicCallbacks.pcs[i]->f);
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
    *((uint32_t*) (idt_entry+6)) = 0;
}

static void init_pit() {
    outb(PIT_CMD, PIT_CHAN_0 | PIT_WORD_RW | PIT_PERIODIC);
    outb(PIT_CH0_DATA, PIT_COUNT & 0xff);
    outb(PIT_CH0_DATA, PIT_COUNT >> 8);
}

// TODO: Grow queue capacities if appropriate
    // TODO:
    // I can have a work queue item that runs periodically (say, every 10 seconds?) that checks capacity
    //   and usage, and doubles capacity if usage is greater than half of capacity.

static void check_queue_caps() {
    //logf("kbd_buf now has capacity: %u\n", kbd_buf.cap);
}

void init_interrupts() {
    cpuCountOffset = read_tsc();
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
    //SET_GETRAP_N(0e);
    set_handler(0x0e, trap_0x0e_page_fault, TYPE_INT);
    SET_GETRAP_N(11);
    SET_GETRAP_N(15);
    SET_GETRAP_N(1d);
    SET_GETRAP_N(1e);

    init_rtc();
    init_pit();
    init_pic();

    INITQ(wq, INIT_WQ_CAP);
    INITQ(kbd_buf, INIT_KB_CAP);

    registerPeriodicCallback((struct periodic_callback) {1, 2, check_queue_caps});

    __asm__ __volatile__("sti");
}
