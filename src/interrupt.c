#include <stdint.h>
//#include "console.h"
#include "serial.h"
#include "hex.h"
#include "io.h"
#include "keyboard.h"

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

struct interrupt_frame
{
    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t sp;
    uint64_t ss;
};

static inline void log(char* s) {
    //print(s);
    print_com1(s);
}

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
    char qs[17];
    qs[16] = '\0';

    log("ip: 0x");
    qwordToHex(frame->ip, qs);
    log(qs);
    log("    cs: 0x");
    qwordToHex(frame->cs, qs);
    log(qs);
    log(" flags: 0x");
    qwordToHex(frame->flags, qs);
    log(qs);
    log("\nsp: 0x");
    qwordToHex(frame->sp, qs);
    log(qs);
    log("    ss: 0x");
    qwordToHex(frame->ss, qs);
    log(qs);
    log("\n");
}

static inline void generic_trap_n(struct interrupt_frame *frame, int n) {
    char bs[3];
    bs[2] = '\0';
    byteToHex(n, bs);

    log("Generic trap handler used for trap vector 0x");
    log(bs);
    log("\n");
    dumpFrame(frame);

    // In generic case, it's not safe to do anything but go to waitloop (well, that may well not be safe either;
    //   halting the machine completly is probably best, but for now I'd like to do it this way rather than
    //   that or risk jumpint to IP.
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

//#define TRAP_HL(h, l) TRAP_N(h##l)

#define TRAP_HL(macro, h, l) macro(h##l)

/*
#define TRAPS_H(h) \
TRAP_HL(h, 0)\
TRAP_HL(h, 1)\
TRAP_HL(h, 2)\
TRAP_HL(h, 3)\
TRAP_HL(h, 4)\
TRAP_HL(h, 5)\
TRAP_HL(h, 6)\
TRAP_HL(h, 7)\
TRAP_HL(h, 8)\
TRAP_HL(h, 9)\
TRAP_HL(h, a)\
TRAP_HL(h, b)\
TRAP_HL(h, c)\
TRAP_HL(h, d)\
TRAP_HL(h, e)\
TRAP_HL(h, f)

TRAPS_H(0)
TRAPS_H(1)
*/

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

// TRAP_N(00)
// TRAP_N(01)
// TRAP_N(02)
// TRAP_N(03)
// TRAP_N(04)
// TRAP_N(05)
// TRAP_N(06)
// TRAP_N(07)
// TRAP_N(08)
// TRAP_N(09)
// TRAP_N(0a)
// TRAP_N(0b)
// TRAP_N(0c)
// TRAP_N(0d)
// TRAP_N(0e)
// TRAP_N(0f)

// static void __attribute__((interrupt)) trap_handler_00(struct interrupt_frame *frame) {
//     //generic_trap_n(frame, 0x00);
// }

static void init_pic() {
    outb(PIC_PRIMARY_CMD, ICW1 | ICW1_ICW4_NEEDED);
    outb(PIC_SECONDARY_CMD, ICW1 | ICW1_ICW4_NEEDED);

    outb(PIC_PRIMARY_DATA, 0x20);   // Map  primary  PIC to 0x20 - 0x27
    outb(PIC_SECONDARY_DATA, 0x28); // Map secondary PIC to 0x28 - 0x2f

    outb(PIC_PRIMARY_DATA, 4);
    outb(PIC_SECONDARY_DATA, 2);

    outb(PIC_PRIMARY_DATA, 1);
    outb(PIC_SECONDARY_DATA, 1);

    outb(PIC_PRIMARY_DATA, 0);
    outb(PIC_SECONDARY_DATA, 0);

    outb(PIC_PRIMARY_DATA, 0xfd);
    outb(PIC_SECONDARY_DATA, 0xff);
}

static void __attribute__((interrupt)) default_interrupt_handler(struct interrupt_frame *frame) {
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    log("Default interrupt handler\n");
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
    char qs[17];
    qs[16] = '\0';

    log("Default trap handler with error on stack; error: 0x");
    qwordToHex(error_code, qs);
    log(qs);
    log("\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) double_fault_handler(struct interrupt_frame *frame, uint64_t error_code) {
    char qs[17];
    qs[16] = '\0';

    log("Double fault; error should be zero.  error: 0x");
    qwordToHex(error_code, qs);
    log(qs);
    log("\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) kbd_irq(struct interrupt_frame *frame) {
    char bs[3];
    bs[2] = '\0';

    uint8_t code = inb(0x60);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    log("C keyboard interrupt handler: ");
    byteToHex(code, bs);
    log(bs);
    log("\n");
    dumpFrame(frame);

    keyScanned(code);
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
}

void init_idt() {
    for (int i = 0; i < 32; i++)
        set_handler(i, default_trap_handler, TYPE_TRAP);
    for (int i = 32; i < 256; i++)
        set_handler(i, default_interrupt_handler, TYPE_INT);

    set_handler(0x21, kbd_irq, TYPE_INT);

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
