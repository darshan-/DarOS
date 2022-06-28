#include <stdarg.h>
#include <stdint.h>

#include "sys.h"

/*
   0: exit
   1: printf
   2: printColor

  */

void exit() {
    asm volatile("\
\n      mov $0, %rax                            \
\n      int $0x80                               \
    ");
}

void printf(char* fmt, ...) {
    va_list ap;
    va_list* app = &ap;
    va_start(ap, fmt);

    asm volatile("\
\n      mov $1, %%rax                           \
\n      mov %0, %%rbx                           \
\n      mov %1, %%rcx                           \
\n      int $0x80                               \
    "::"m"(fmt),"m"(app));
    va_end(ap);
}

void printColor(char* s, uint8_t c) {
    asm volatile("\
\n      mov $2, %%rax                           \
\n      mov %0, %%rbx                           \
\n      movb %1, %%cl                           \
\n      int $0x80                               \
    "::"m"(s),"m"(c));
}

extern void main();

void __attribute__((section(".entry"))) _entry() {
    main();
    exit();
}
