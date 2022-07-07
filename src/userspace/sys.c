#include <stdarg.h>
#include <stdint.h>

#include "sys.h"

#include "../lib/malloc.h"
#include "../lib/strings.h"

/*
   0: exit
   1: printf
   2: printColor
   3: readline

  */

void exit() {
    print("Exiting...\n");
    asm volatile("\
\n      mov $0, %rax                            \
\n      int $0x80                               \
    ");
}

void wait(uint64_t p) {
    p = p;
}

uint64_t runProg(char* s) {
    s = s;
    return 0;
}

void printColor(char* s, uint8_t c) {
    asm volatile("\
\n      mov $2, %%rax                           \
\n      mov %0, %%rbx                           \
\n      movb %1, %%cl                           \
\n      int $0x80                               \
    "::"m"(s),"m"(c));
}

void print(char* s) {
    printColor(s, 0x07);
}

void printf(char* fmt, ...) {
    VARIADIC_PRINT(print);
}

char* M_readline() {
    uint64_t len; // Kernel needs to know len in order to know where to put it on stack, so use that knowledge (to malloc needed size)
    char* l;
    asm volatile("\
\n      mov $3, %%rax                           \
\n      int $0x80                               \
\n      mov %%rax, %0                           \
\n      mov %%rbx, %1                           \
    ":"=m"(len), "=m"(l));

    char* s = malloc(len + 1);
    for (uint64_t i = 0; i < len; i++)
        s[i] = l[i];
    s[len] = 0;

    return s;
}

extern void main();

void __attribute__((section(".entry"))) _entry() {
    // I think I might prefer to use linker to place map last in text section, and have heap grow up toward stack, and have stack at end
    //   of page...

    init_heap((uint64_t*) 0x7FC0180000ull, 0x80000);
    main();
    exit();
}
