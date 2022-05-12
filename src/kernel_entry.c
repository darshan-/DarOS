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

void dumpMem(uint8_t* start, int count) {
    char* s = " ";
    for (int i = 0; i < count; i++) {
        uint8_t ch = (*start >> 4) + '0';
        uint8_t cl = (*start++ & 0x0f) + '0';
        if (ch > '9')
            ch += 'A' - '9' - 1;
        s[0] = ch;
        print(s);
        if (cl > '9')
            cl += 'A' - '9' - 1;
        s[0] = cl;
        print(s);

        if (i % 4 == 3) {
            s[0] = ' ';
            print(s);
        }
        if (i % 16 == 15) {
            s[0] = '\n';
            print(s);
        }
    }
}

struct interrupt_frame
{
    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t sp;
    uint64_t ss;
};

/*
        mov al, 0x20
        out PIC_PRIMARY_CMD, al

 */


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

static uint64_t testResume;

void __attribute__((naked)) waitloop() {
    __asm__ __volatile__(
                         "loop:\n"
                         "hlt\n"
                         "jmp loop\n"
    );
    // printColor("WAIT LOOP\n", 0x0d);
    // while(1) {
    // printColor("Loop top\n", 0x0f);
    // __asm__ __volatile__("hlt\n");
    //     printColor("Loop bottom\n", 0x0f);
    // }
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
    //frame->ip += 2; // Hacky and unsafe, is my understanding, but helps test my understanding for now
    __asm__ __volatile__(
                         "mov $0x20, %al\n"
                         "out %al, $0x20\n"
    );
    //frame->ip = waitloop;
    frame->ip = testResume;
    dumpFrame(frame);
    //__asm__ __volatile__("int $0x03\n");
}

void __attribute__((interrupt)) default_trap_handler(struct interrupt_frame *frame) {
    __asm__ __volatile__(
                         "mov $0x20, %al\n"
                         "out %al, $0x20\n"
    );
    printColor("Default trap handler\n", 0x0e);
    dumpFrame(frame);
    //__asm__ __volatile__("int $0x03\n");
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

// void __attribute__((interrupt)) default_trap_with_error_handler(struct interrupt_frame *frame) {
//     uint64_t error;
//     __asm__ __volatile__(
//                          "pop %%rax\n"
//                          "mov %%rax, %0\n"
//                          :"=m"(error)
//     );
//     printColor("Default trap handler with error on stack; error: 0x", 0x0e);
//     printQword(error);
//     print("\n");
//     dumpFrame(frame);
// }

void __attribute__((interrupt)) kbd_irq(struct interrupt_frame *frame) {
    uint8_t code;
    // Need double percent signs when using contraints!
    __asm__ __volatile__(
                         "in $0x60, %%al\n"
                         "mov %%al, %0\n"
                         "mov $0x20, %%al\n"
                         "out %%al, $0x20\n"
                         //"push %%rax\n"
                         //"int $0x0b\n"
                         //"int $0x03\n"
                         //"mov %%rax, %%cr1\n"
                         "xor %%rax, %%rax\n"
                         "mov %%ss, %%rax\n"
                         //"push %%rax\n"
                         "mov $0xff, %%ax\n"
                         //"mov %%ax, %%ss\n"
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

    frame->ip = (uint64_t) waitloop;

    //__asm__("in $0x60, %al\n"
    //        "movl %%eax, %0\n" :"=r"(code));
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

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    //clearScreen();
    //print("Kernel launched!\n");
    //return;

    //__asm__("int $3");
    printColor("Running 64-bit kernel written in C!\n", 0x0d);
    printQword(0x0123456789abcdef);
    print("\n");
    //set_kbd_irq();

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

    //waitloop();

    //__asm__("int $3");
    //__asm__("int $3");

    //dumpMem(0, 16*40);

    //print("\n\n\n\n\n                  ");
    //printColor("Cool!", 0x3f);
    //for(int i=0; i<25; i++) print("0123456789abcdefghijklmnopqrstuvwxyz!\n");
    //print("Hi!\n");
    //print("01234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
    //print("01234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
}
