#include <stdint.h>

#include "malloc.h"

#include "interrupt.h"

#include "log.h"
#include "strings.h"

#define BLK_SZ 128
#define MAP_ENTRY_SZ 2
#define MAP_FACTOR (64 / MAP_ENTRY_SZ * BLK_SZ)
#define BFREE 0b00ull
#define BPART 0b11ull
#define BEND  0b10ull

static uint64_t map_size; // Size in quadwords
static uint64_t* map = (uint64_t*) 0;
static uint64_t* heap = (uint64_t*) 0;

// `size' is the number of bytes available to us for (map + heap)
void init_heap(uint64_t* start, uint64_t size) {
    map_size = size / (1 + MAP_FACTOR);
    map = start;
    map[0] = 0;
    for (uint64_t i = 0; i < map_size; i++)
        map[i] = 0;
    heap = map + map_size;
}

uint64_t memUsed() {
    uint64_t p = 0;
    for (uint64_t i = 0; i < map_size; i++) {
        uint64_t entry = map[i];

        if (entry == 0) continue;

        for (uint64_t j = 0; j < (64 / MAP_ENTRY_SZ); j++, entry >>= MAP_ENTRY_SZ)
            if (entry & 0b11)
                p++;
    }

    return p * BLK_SZ;
}

// Returns number of bytes in the heap (not counting map)
uint64_t heapSize() {
    return map_size * MAP_FACTOR;
}

void* malloc(uint64_t nBytes) {
    if (heap == 0)
        return 0;

    uint64_t needed = (nBytes / BLK_SZ) + !!(nBytes % BLK_SZ);
    uint64_t mask = 0;

    for (uint64_t i = 0; i < needed && mask != -1ull; i++)
        mask = (mask << 2) + 0b11;

    void* ret = 0;
    no_ints();
    for (uint64_t i = 0; i < map_size; i++) {
        uint64_t need;
        for (need = needed; need > 64 / MAP_ENTRY_SZ; i++) {
            if (map[i]) {
                need = needed;
                ret = 0;
                continue;
            }

            if (!ret)
                ret = map[i];

            need -= 64 / MAP_ENTRY_SZ;
        }

        mask = 0;
        for (uint64_t j = 0; j < need; j++)
            mask = (mask << 2) + 0b11;

        for (uint64_t j = 0; j < 64 / MAP_ENTRY_SZ - (need - 1); j++, mask <<= 2) {
            if (!(map[i] & mask)) {
                mask ^= 1ull << ((need - 1 + j) * 2);

                map[i] |= mask;
                ret = (void*) (uint64_t) heap + (i * (64 / MAP_ENTRY_SZ) + j) * BLK_SZ;

                ints_okay();
                return ret;
            } else if (needed > 64 / MAP_ENTRY_SZ) {
                
            }
        }
    }

    ints_okay();
    return 0;
}

void free(void *p) {
    if (p < (void*) heap || p > (void*) heap + (map_size * (64 / MAP_ENTRY_SZ) - 1) * BLK_SZ)
        return;

    uint64_t b = (uint64_t) p - (uint64_t) heap;
    uint64_t o = (b % MAP_FACTOR) / BLK_SZ * MAP_ENTRY_SZ;
    b /= MAP_FACTOR;

    for (uint64_t mask = 0b11ull << o;; b++, mask = 0b11ull, o = 0) {
        for (; mask && (map[b] & mask) == BPART << o; mask <<= 2, o += 2)
            map[b] &= ~mask;

        if ((map[b] & mask) == BEND << o) {
            map[b] &= ~mask;
            break;
        }
    }
}

void* realloc(void* p, int newSize) {
    if (heap == 0) return 0;

    uint64_t* q1 = (uint64_t*) p;
    uint64_t* q2 = malloc(newSize);
    uint8_t* b1 = (uint8_t*) p;
    uint8_t* b2 = (uint8_t*) q2;

    // TODO, with map, we now know how many blocks p is, so don't copy past end of it

    int i, j;
    for (i = 0; i < newSize / 8; i++)
        q2[i] = q1[i];
    for (j = i*8; j < i + newSize % 8; j++)
        b2[j] = b1[j];

    free(p);
    return q2;
}
