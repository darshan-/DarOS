#pragma once

#include <stdint.h>

#include "serial.h"

void init_interrupts();
void waitloop();

extern uint64_t int_blocks;

static inline void no_ints() {
    __asm__ __volatile__("cli");
    int_blocks++;
}

static inline void ints_okay() {
    if (int_blocks <= 0) {
        com1_printf("WARNING: ok_ints() called when int_blocks was %u... You have a bug.\n", int_blocks);
        return;
    }

    int_blocks--;

    if (int_blocks == 0)
        __asm__ __volatile__("sti");
}
