#include <stdarg.h>
#include <stdint.h>
#include "serial.h"
#include "io.h"
#include "keyboard.h"
#include "rtc.h"


/*

  Okay, I think I'm finally setting up and using the PICs correctly.

  If IRQ was initiated by the secondary, that means both are involved, and both need an ack.
  If it was initiated by the primary, just ack to that one.

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

#define KERNEL_STACK_TOP 0xeffff
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


#define log com1_printf

// void log(char* fmt, ...) {
//     fmt = fmt;
// }

void __attribute__((naked)) waitloop() {
    __asm__ __volatile__(
        "mov $"
        QUOTE(KERNEL_STACK_TOP)
        ", %esp\n" // We'll never return anywhere or use anything currently on the stack, so reset it
        "loop:\n"
        "sti\n"
        "hlt\n"
        "jmp loop\n"
    );
}

static void dumpFrame(struct interrupt_frame *frame) {
    log("ip: 0x%16h    cs: 0x%16h flags: 0x%16h\n", frame->ip, frame->cs, frame->flags);
    log("sp: 0x%16h    ss: 0x%16h\n", frame->sp, frame->ss);
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

// Is there an easier/cleaner/more efficient way to do this?  Macros are weird, clunky, too powerful and too
//   limited at the same time...

#define SET_GTRAP_N(nn) set_handler(0x##nn, trap_handler_0x##nn, TYPE_TRAP);

#define TRAP_N(nn) static void __attribute__((interrupt)) trap_handler_0x##nn(struct interrupt_frame *frame) {\
    generic_trap_n(frame, 0x##nn);\
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
    outb(PIC_PRIMARY_DATA, 1);      // PIT is firing by default (maybe no matter what?); ignore it, at least for now.
    outb(PIC_SECONDARY_DATA, 0);
}

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

static void __attribute__((interrupt)) default_trap_handler(struct interrupt_frame *frame) {
    log("Default trap handler\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) default_trap_with_error_handler(struct interrupt_frame *frame,
                                                                       uint64_t error_code) {
    log("Default trap handler with error on stack;  error: 0x%16h\n", error_code);
    dumpFrame(frame);
}

static void __attribute__((interrupt)) double_fault_handler(struct interrupt_frame *frame, uint64_t error_code) {
    log("Double fault; error should be zero.  error: 0x%16h\n", error_code);
    dumpFrame(frame);
}

static void __attribute__((interrupt)) irq1_kbd(struct interrupt_frame *frame) {
    uint8_t code = inb(0x60);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    log("C keyboard interrupt handler: %2h\n", code);
    dumpFrame(frame);

    keyScanned(code);
}

static uint64_t rtcCount = 0;

static void __attribute__((interrupt)) irq8_rtc(struct interrupt_frame *frame) {
    uint8_t type = irq8_type();
    outb(PIC_SECONDARY_CMD, PIC_ACK);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    if (type == RTC_INT_PERIODIC) {
        rtcCount += 1;
        if (rtcCount % 1024 == 0)
            printf("rtcCount: %u\n", rtcCount);
    }
}

static void __attribute__((interrupt)) irq0_pit(struct interrupt_frame *frame) {
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    log("irq0 (PIT!) interrupt handler\n");
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

void init_idt() {
    for (int i = 0; i < 32; i++)
        set_handler(i, default_trap_handler, TYPE_TRAP);
    for (int i = 32; i < 40; i++)
        set_handler(i, default_PIC_P_handler, TYPE_INT);
    for (int i = 40; i < 48; i++)
        set_handler(i, default_PIC_S_handler, TYPE_INT);
    for (int i = 48; i < 256; i++)
        set_handler(i, default_interrupt_handler, TYPE_INT);

    set_handler(0x20, irq0_pit, TYPE_INT);
    set_handler(0x21, irq1_kbd, TYPE_INT);
    set_handler(0x28, irq8_rtc, TYPE_INT);

    // These are the traps with errors on stack according to https://wiki.osdev.org/Exceptions, 
    set_handler(8, double_fault_handler, TYPE_TRAP);
    for (int i = 10; i <= 14; i++)
        set_handler(i, default_trap_with_error_handler, TYPE_TRAP);
    set_handler(17, default_trap_with_error_handler, TYPE_TRAP);
    set_handler(21, default_trap_with_error_handler, TYPE_TRAP);
    set_handler(29, default_trap_with_error_handler, TYPE_TRAP);
    set_handler(30, default_trap_with_error_handler, TYPE_TRAP);

    TRAPS(SET_GTRAP_N, 0);
    TRAPS(SET_GTRAP_N, 1);

    set_handler(0, divide_by_zero_handler, TYPE_TRAP);

    init_pic();
}
