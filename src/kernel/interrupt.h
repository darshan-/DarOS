#pragma once

#include <stdint.h>

#include "log.h"

void init_interrupts();
void waitloop();
uint64_t startSh(uint64_t stdout);
void gotLine(void* p, char* l);

extern uint64_t int_blocks;
extern uint64_t* kernel_stack_top;

static inline void no_ints() {
    asm volatile("cli");
    int_blocks++;
}

static inline void ints_okay_once_on() {
    if (int_blocks <= 0) {
        logf("WARNING: ok_ints() called when int_blocks was %u... You have a bug.\n", int_blocks);
        return;
    }

    int_blocks--;
}

static inline void ints_okay() {
    ints_okay_once_on();

    if (int_blocks == 0)
        asm volatile("sti");
}
