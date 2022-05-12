#include <stdint.h>
#include "io.h"

void outb(uint32_t dest, uint8_t val) {
    __asm__ __volatile__(
        "mov %0, %%al\n"
        "mov %1, %%dx\n"
        "out %%al, %%dx\n"
        :"=m"(val), "=m"(dest)
    );
}

void outw(uint32_t dest, uint16_t val) {
    __asm__ __volatile__(
        "mov %0, %%ax\n"
        "mov %1, %%dx\n"
        "out %%ax, %%dx\n"
        :"=m"(val), "=m"(dest)
    );
}

uint8_t inb(uint32_t source) {
    uint8_t val;

    __asm__ __volatile__(
        "mov %0, %%dx\n"
        "in %%dx, %%al\n"
        "mov %%al, %1\n"
        :"=m"(source), "=m"(val)
    );

    return val;
}

uint16_t inw(uint32_t source) {
    uint16_t val;

    __asm__ __volatile__(
        "mov %0, %%dx\n"
        "in %%dx, %%ax\n"
        "mov %%ax, %1\n"
        :"=m"(source), "=m"(val)
    );

    return val;
}
