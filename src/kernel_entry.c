#include <stdint.h>
#include "console.h"
#include "interrupt.h"
#include "serial.h"

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

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    printColor("Running 64-bit kernel written in C!\n", 0x0d);

    print_com1("This is a test!\nI wonder if control characters are supported?");
    print_com1("\r\nYes, it appears they are -- albeit with the caveat that newline is ");
    print_com1("*just* newline, so let's try with carriage return as well...\r\n");

    for (int i = 0; i < 50; i++)
        print_com1("Let's see if QEMU does scrolling for us...  Seems likely, and would suck if not.\n");

    print_com1("So, how'd we do???\n");

    init_idt();

    waitloop();

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
