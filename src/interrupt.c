#include <stdint.h>
#include "console.h"

/*
  I can't fucking tell for the life of me of sending 0x20 to 0x20 (primary PIC) is something that should be
    done for all interrupts, or only ones at or above 0x20 (software interrupts), or something else.
  It seems bad to send if the PIC isn't involved; we only want to send it if PIC is expecting it for
    what we're handling.
  It's also not clear whether just sending it to 0x20 is always all we need, or if we sometimes need to
    send it to 0x21 too.
  I keep searching the web and finding diffent little breadcrumbs and clues, but even the best articles I'm
    finding are super vague on a lot of these details.  The 8259A manual is helpful, but at the other exteme
    of so much info it's hard to aswer these questions, some of which may be beyond the scope of that very
    thorough document in any case.
 */

struct interrupt_frame
{
    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t sp;
    uint64_t ss;
};

static uint64_t testResume;

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

void printEflags() {
    // mov eflags into general-purpose register and then into local variable, then print it.
}

void __attribute__((naked)) waitloop() {
    __asm__ __volatile__(
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

// I think if the divid-by-zero happened in kernel mode, our best bet is to halt the machine.  Like, turn off
//   interrupts and halt.  (cli; hlt).  If in user mode, of course we can send a signal to, or, if necessary,
//   terminate, the process.  But in kernel mode, resuming isn't really a reasonable option, and dividing by
//   zero is a bug that should be fixed.
/*
  Wait, I *still* think I'm unclear on why setting frame->ip to waitloop doesn't work from keyboard handler
    but does (it does, right?) from outside a handler.  There should be a bit of wasted space on the stack if
    we never return from the outer interrupt that caused the divid-by-zero, but if halt, and then another
    interrupt comes in, which should still be in a functional state, but we seem frozen.  Wait... are interrupts
    stopped while we're there?  Well, no, because we ended up in the divide-by-zero handler.

  Hmm, maybe there's other preamble and <hmm, post-amble?> that the compiler generates, leaving us in a weird state
    if we never finish the function?  Or does iret really need to happen?

  Hmm, perhaps EFLAGS bit 9?  Interrupt enable?  That might be disabled.  iret pops eflags, so it's not just about
    stack space.  Hmm, at least moderately likely, I think!
  */
void __attribute__((interrupt)) divide_by_zero_handler(struct interrupt_frame *frame) {
    printColor("Divide by zero handler\n", 0x0e);
    __asm__ __volatile__(
                         "mov $0x20, %al\n"
                         "out %al, $0x20\n"
    );
    frame->ip = (uint64_t) waitloop;
    //frame->ip = testResume;
    dumpFrame(frame);
}

void __attribute__((interrupt)) default_trap_handler(struct interrupt_frame *frame) {
    __asm__ __volatile__(
                         "mov $0x20, %al\n"
                         "out %al, $0x20\n"
    );
    printColor("Default trap handler\n", 0x0e);
    dumpFrame(frame);
}

void __attribute__((interrupt)) default_trap_with_error_handler(struct interrupt_frame *frame, uint64_t error_code) {
    __asm__ __volatile__(
                         "mov $0x20, %al\n"
                         "out %al, $0x20\n"
    );
    printColor("Default trap handler with error on stack; error: 0x", 0x0e);
    printQword(error_code);
    print("\n");
    dumpFrame(frame);
}

void __attribute__((interrupt)) double_fault_handler(struct interrupt_frame *frame, uint64_t error_code) {
    __asm__ __volatile__(
                         "mov $0x20, %al\n"
                         "out %al, $0x20\n"
    );
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

    if (code == 0x0b) {
        testResume = (uint64_t) &&testResumel;
        printColor("Dividing by zero...", 0x0d);
        print("\n");
        int x = 0;
        int y = 1/x;
    testResumel:
        printColor("Divided by zero...", 0x0f);
        print("\n");
    }

    //frame->ip = (uint64_t) waitloop;
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

    testResume = (uint64_t) &&testResumel;
    printColor("Dividing by zero...", 0x0d);
    print("\n");
    int x = 0;
    int y = 1/x;
 testResumel:
    printColor("Divided by zero...", 0x0f);
    print("\n");

    testResume = (uint64_t) &&testResumel2;
    printColor("Dividing by zero...", 0x0d);
    print("\n");
    y = 1/x;
 testResumel2:
    printColor("Divided by zero...", 0x0f);
    print("\n");
}
