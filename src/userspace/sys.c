#include <stdarg.h>

#include "sys.h"

/*
   0: exit
   1: printf

  */

void exit() {
    asm volatile("\
\n      mov $0, %rax                            \
    ");
    asm volatile("\
\n      int $0x80                               \
    ");
}

void printf(char* fmt, ...) {
    va_list ap;
    va_list* app = &ap;
    va_start(ap, fmt);
    //void* v = va_arg(ap, void*);

    // Call kernel with fmt and ap...
    asm volatile("\
\n      mov $1, %%rax                           \
\n      mov %0, %%rbx                           \
\n      mov %1, %%rcx                           \
\n      int $0x80                               \
    "::"m"(fmt),"m"(app));
    va_end(ap);
}
extern void main();
void __attribute__((section(".entry"))) _entry() {
    main();
    exit();
}
