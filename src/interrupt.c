#include <stdint.h>
#include "console.h"

struct interrupt_frame
{
    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t sp;
    uint64_t ss;
};

void dumpFrame(struct interrupt_frame *frame) {
    print("ip: 0x");
    printQword(frame->ip);
    print("    cs: 0x");
    printQword(frame->cs);
    print(" flags: 0x");
    printQword(frame->flags);
    print("\nsp: 0x");
    printQword(frame->sp);
    print("    ss: 0x");
    printQword(frame->ss);
    print("\n");
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

void __attribute__((interrupt)) default_interrupt_handler(struct interrupt_frame *frame) {
    __asm__ __volatile__(
        "mov $0x20, %al\n"
        "out %al, $0x20\n"
    );

    printColor("Default interrupt handler\n", 0x0d);
    dumpFrame(frame);
}

void __attribute__((interrupt)) divide_by_zero_handler(struct interrupt_frame *frame) {
    printColor("Divide by zero handler\n", 0x0e);
    frame->ip = (uint64_t) waitloop;
    dumpFrame(frame);
}

void __attribute__((interrupt)) default_trap_handler(struct interrupt_frame *frame) {
    printColor("Default trap handler\n", 0x0e);
    dumpFrame(frame);
}

void __attribute__((interrupt)) default_trap_with_error_handler(struct interrupt_frame *frame, uint64_t error_code) {
    printColor("Default trap handler with error on stack; error: 0x", 0x0e);
    printQword(error_code);
    print("\n");
    dumpFrame(frame);
}

void __attribute__((interrupt)) double_fault_handler(struct interrupt_frame *frame, uint64_t error_code) {
    printColor("Double fault; error should be zero.  error: 0x", 0x3e);
    printQword(error_code);
    print("\n");
    dumpFrame(frame);
}

void __attribute__((interrupt)) kbd_irq(struct interrupt_frame *frame) {
    uint8_t code;
    // Need double percent signs when using contraints!
    __asm__ __volatile__(
        "in $0x60, %%al\n"
        "mov %%al, %0\n"
        "mov $0x20, %%al\n"
        "out %%al, $0x20\n"
        :"=m"(code)
    );

    printColor("C keyboard interrupt handler: ", 0x0d);
    printByte(code);
    print("\n");
    dumpFrame(frame);
}

void set_handler(uint16_t* idt_entry, void* handler) {
    uint64_t offset = (uint64_t) handler;
    *idt_entry = (uint16_t) (offset & 0xffff);
    offset >>= 16;
    idt_entry += 3;
    *idt_entry++ = (uint16_t) (offset & 0xffff);
    offset >>= 16;
    *idt_entry = (uint32_t) offset;
}

void init_idt() {
    for (int i = 0; i < 32; i++)
        set_handler((uint16_t*) (uint64_t) (16 * i), default_trap_handler);
    for (int i = 32; i < 256; i++)
        set_handler((uint16_t*) (uint64_t)  (16 * i), default_interrupt_handler);

    set_handler((uint16_t*) (16 * 0x21), kbd_irq);

    // Based on https://wiki.osdev.org/Exceptions, these are the traps with errors on stack
    set_handler((uint16_t*) (16 * 8), double_fault_handler);
    for (int i = 10; i <= 14; i++)
        set_handler((uint16_t*)  (uint64_t) (16 * i), default_trap_with_error_handler);
    set_handler((uint16_t*) (16 * 17), default_trap_with_error_handler);
    set_handler((uint16_t*) (16 * 21), default_trap_with_error_handler);
    set_handler((uint16_t*) (16 * 29), default_trap_with_error_handler);
    set_handler((uint16_t*) (16 * 30), default_trap_with_error_handler);

    set_handler((uint16_t*) (16 * 0), divide_by_zero_handler);
}
