#include "io.h"
#include "serial.h"

#define COM1 0x3f8

static void write_com1(char c) {
    //while (inb(COM1 + 5) & 0x20 == 0) // Wait until transmitter holding register is empty and ready for data
    while ((inb(COM1 + 5) & 0x20) == 0) // Wait until transmitter holding register is empty and ready for data
        ;

    // Qemu's serial port display interprets control characters, but \n just drops down a line, so we want a
    //   carriage return as well;
    if (c == '\n')
        outb(COM1, '\r');

    outb(COM1, c);
}

void print_com1(char* s) {
    while (*s != 0)
        write_com1(*s++);
}
