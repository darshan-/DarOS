#include <stdint.h>
#include "malloc.h"

// Let's say for now that the first 9 MB starting at 1 MB are for malloc
#define BOTTOM (uint8_t*) 0x100000 //  1 MB
#define    TOP (uint8_t*) 0xffffff // 10 MB - 1

static uint8_t* cur = BOTTOM;

void* malloc(int nBytes) {
    uint8_t* tentative = cur + nBytes;

    if (tentative > TOP)
        return 0;

    cur = tentative;
    updateMallocCur(cur);

    return cur;
}

// Let's just leak memory but get to have dynamic allocation for now
void free(void*) {
}

void* realloc(void* p, int newSize) {
    uint64_t* q1 = (uint64_t*) p;
    uint64_t* q2 = malloc(newSize);
    uint8_t* b1 = (uint8_t*) p;
    uint8_t* b2 = (uint8_t*) q2;

    // We'll want to know the size of p in the future and not copy more than that
    // for (int i = 0; i < newSize / 8 + (newSize % 8 ? 1 : 0); i++) // Nope, not safe
    //     p2[i] = p1[i];

    int i, j;
    for (i = 0; i < newSize / 8; i++)
        q2[i] = q1[i];
    for (j = i*8; j < i + newSize % 8; j++)
        b2[j] = b1[j];

    free(p);
    return q2;
}
