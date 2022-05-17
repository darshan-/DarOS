#include <stdarg.h>
#include "io.h"
#include "malloc.h"
#include "serial.h"
#include "strings.h"

#define COM1 0x3f8

static void com1_write(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) // Wait until transmitter holding register is empty and ready for data
        ;

    // Qemu's serial port display interprets control characters, but \n just drops down a line, so we want a
    //   carriage return as well;
    if (c == '\n')
        outb(COM1, '\r');

    outb(COM1, c);
}

void com1_print(char* s) {
    while (*s != 0)
        com1_write(*s++);
}

void com1_printf(char* fmt, ...) {
    VARIADIC_PRINT(com1_print);
}
