#include <stdarg.h>

#include "serial.h"

#include "interrupt.h"
#include "io.h"
#include "malloc.h"

#include "../lib/strings.h"

#define COM1 0x3f8

void com1_write(char c) {
    no_ints();
    while ((inb(COM1 + 5) & 0x20) == 0) // Wait until transmitter holding register is empty and ready for data
        ;

    // Qemu's serial port display interprets control characters, but \n just drops down a line, so we want a
    //   carriage return as well;
    if (c == '\n')
        outb(COM1, '\r');

    outb(COM1, c);
    ints_okay();
}

void com1_print(char* s) {
    while (*s != 0)
        com1_write(*s++);
}

void com1_printf(char* fmt, ...) {
    VARIADIC_PRINT(com1_print);
}

// From https://wiki.osdev.org/index.php?title=Serial_Ports&oldid=19791
void init_com1() {
   outb(COM1 + 1, 0x00);    // Disable all interrupts
   outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   outb(COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   outb(COM1 + 1, 0x00);    //                  (hi byte)
   outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
   outb(COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}
