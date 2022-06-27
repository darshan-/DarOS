#include <stdint.h>

static uint64_t a;
static uint64_t b;

static void fibNext() {
    uint64_t c = a;
    a = b;
    b = a + c;
}

void __attribute__((section(".entry"))) main() {
    a = 1;
    b = 1;

    for (;;) {
        asm volatile("movq %0, %%r14"::"m"(a));
        asm volatile("movq %0, %%r15"::"m"(b));
        if (b < 10000000000000000000ull)
            fibNext();
    }
}
