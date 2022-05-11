#include <stdint.h>
#include "console.h"

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
void __attribute__((interrupt)) kbd_irq(struct interrupt_frame *frame) {
    uint8_t code;
    __asm__ __volatile__(
                         "in $0x60, %%al\n"
                         "mov %%al, %0\n"
                         "mov $0x20, %%al\n"
                         "out %%al, $0x20\n"
                         :"=m"(code)
    );
    char* s = "C keyboard interrupt handler... \n";
    s[31] = code;
    printColor(s, 0x0d);
    //__asm__("in $0x60, %al\n"
    //        "movl %%eax, %0\n" :"=r"(code));
}

/*
        mov rax, keyboard_gate
        mov ebx, idt + (16 * 0x21)
        mov [ebx], ax
        shr rax, 16
        mov [ebx+6], rax

 */
void set_kbd_irq() {
    uint16_t* irq1 = (uint16_t*) (16 * 0x21);
    uint64_t offset = (uint64_t) kbd_irq;
    *irq1 = (uint16_t) (offset & 0xffff);
    offset >>= 16;
    irq1 += 3;
    *irq1 = (uint16_t) (offset & 0xffff);
    irq1++;
    offset >>= 16;
    *irq1 = (uint32_t) offset;

    //dumpMem(16 * 0x21, 16*3);
}

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    //clearScreen();
    //print("Kernel launched!\n");
    //return;

    //__asm__("int $3");
    printColor("Running 64-bit kernel written in C!\n", 0x0d);
    set_kbd_irq();
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
