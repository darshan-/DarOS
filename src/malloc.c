#include <stdint.h>
#include "malloc.h"

// Let's say for now that the first 9 MB starting at 1 MB are for malloc
#define BOTTOM (uint8_t*) 0x100000 //  1 MB
#define    TOP (uint8_t*) 0xffffff // 10 MB - 1

static uint8_t* cur = BOTTOM;

void *malloc(int nBytes) {
    print_com1("malloc top\n");
    uint8_t* tentative = cur + nBytes;

    if (tentative > TOP)
        return 0;

    cur = tentative;
    print_com1("malloc bottom\n");
    return cur;
}

// Let's just leak memory but get to have dynamic allocation for now
void free(void*) {
}
