#include <stdint.h>
#include "console.h"
#include "hexoutput.h"
#include "io.h"

#define TYPE_TRAP 0b1111
#define TYPE_INT 0b1110

#define PIC_ACK 0x20

#define ICW1 1<<4
#define ICW1_ICW4_NEEDED 1

#define PIC_PRIMARY_CMD 0x20
#define PIC_PRIMARY_DATA 0x21
#define PIC_SECONDARY_CMD 0xa0
#define PIC_SECONDARY_DATA 0xa1

struct interrupt_frame
{
    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t sp;
    uint64_t ss;
};

void cprintChar(uint8_t) {
}

static void dumpFrame(struct interrupt_frame *frame) {
    print("ip: 0x");
    hexoutQword(frame->ip, cprintChar);
    print("    cs: 0x");
    hexoutQword(frame->cs, cprintChar);
    print(" flags: 0x");
    hexoutQword(frame->flags, cprintChar);
    print("\nsp: 0x");
    hexoutQword(frame->sp, cprintChar);
    print("    ss: 0x");
    hexoutQword(frame->ss, cprintChar);
    print("\n");
}

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

void __attribute__((naked)) waitloop() {
    __asm__ __volatile__(
        "mov $0x7bff, %esp\n" // We'll never return anywhere or use anything currently on the stack, so reset it
        "loop:\n"
        "sti\n"
        "hlt\n"
        "jmp loop\n"
    );
}

static void __attribute__((interrupt)) default_interrupt_handler(struct interrupt_frame *frame) {
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    printColor("Default interrupt handler\n", 0x0d);
    dumpFrame(frame);
}

static void __attribute__((interrupt)) divide_by_zero_handler(struct interrupt_frame *frame) {
    printColor("Divide by zero handler\n", 0x0e);
    frame->ip = (uint64_t) waitloop;
    dumpFrame(frame);
}

static void __attribute__((interrupt)) default_trap_handler(struct interrupt_frame *frame) {
    printColor("Default trap handler\n", 0x0e);
    dumpFrame(frame);
}

static void __attribute__((interrupt)) default_trap_with_error_handler(struct interrupt_frame *frame,
                                                                       uint64_t error_code) {
    printColor("Default trap handler with error on stack; error: 0x", 0x0e);
    hexoutQword(error_code, cprintChar);
    print("\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) double_fault_handler(struct interrupt_frame *frame, uint64_t error_code) {
    printColor("Double fault; error should be zero.  error: 0x", 0x3e);
    hexoutQword(error_code, cprintChar);
    print("\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) kbd_irq(struct interrupt_frame *frame) {
    uint8_t code = inb(0x60);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    printColor("C keyboard interrupt handler: ", 0x0d);
    hexoutByte(code, cprintChar);
    print("\n");
    dumpFrame(frame);
}

static void set_handler(uint16_t* idt_entry, void* handler, uint8_t type) {
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
        set_handler((uint16_t*) (uint64_t) (16 * i), default_trap_handler, TYPE_TRAP);
    for (int i = 32; i < 256; i++)
        set_handler((uint16_t*) (uint64_t)  (16 * i), default_interrupt_handler, TYPE_INT);

    set_handler((uint16_t*) (16 * 0x21), kbd_irq, TYPE_INT);

    // These are the traps with errors on stack according to https://wiki.osdev.org/Exceptions, 
    set_handler((uint16_t*) (16 * 8), double_fault_handler, TYPE_TRAP);
    for (int i = 10; i <= 14; i++)
        set_handler((uint16_t*)  (uint64_t) (16 * i), default_trap_with_error_handler, TYPE_TRAP);
    set_handler((uint16_t*) (16 * 17), default_trap_with_error_handler, TYPE_TRAP);
    set_handler((uint16_t*) (16 * 21), default_trap_with_error_handler, TYPE_TRAP);
    set_handler((uint16_t*) (16 * 29), default_trap_with_error_handler, TYPE_TRAP);
    set_handler((uint16_t*) (16 * 30), default_trap_with_error_handler, TYPE_TRAP);

    set_handler((uint16_t*) (16 * 0), divide_by_zero_handler, TYPE_TRAP);

    init_pic();
}
