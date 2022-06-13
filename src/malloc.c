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

uint64_t heapUsed() {
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

    uint64_t need = needed;
    void* ret = 0;

    no_ints();
    for (uint64_t i = 0; i < map_size; i++) {
        for (need = needed; need > 64 / MAP_ENTRY_SZ; i++) {
            if (map[i])
                goto not_found;

            if (!ret)
                ret = (void*) (uint64_t) heap + (i * 64 / MAP_ENTRY_SZ) * BLK_SZ;

            need -= 64 / MAP_ENTRY_SZ;
        }

        mask = 0;
        for (uint64_t j = 0; j < need; j++)
            mask = (mask << 2) + 0b11;

        for (uint64_t j = 0; j < 64 / MAP_ENTRY_SZ - (need - 1); j++, mask <<= 2) {
            if (!(map[i] & mask)) {
                mask ^= 1ull << ((need - 1 + j) * 2);

                if (!ret)
                    ret = (void*) (uint64_t) heap + (i * (64 / MAP_ENTRY_SZ) + j) * BLK_SZ;

                map[i] |= mask;
                for (; needed > 64 / MAP_ENTRY_SZ; needed -= 64 / MAP_ENTRY_SZ, i--)
                    map[i-1] = -1ull;

                ints_okay();
                return ret;
            } else if (needed > 64 / MAP_ENTRY_SZ) {
                goto not_found;
            }
        }

    not_found:
        need = needed;
        ret = 0;
    }

    ints_okay();
    return 0;
}

void* mallocz(uint64_t nBytes) {
    uint64_t* p = malloc(nBytes);
    if (!p)
        return 0;

    for (uint64_t i = 0; i < nBytes / 8 + !!(nBytes % 8); i++)
        p[i] = 0;

    return p;
}

void free(void *p) {
    if (p < (void*) heap || p > (void*) heap + (map_size * (64 / MAP_ENTRY_SZ) - 1) * BLK_SZ)
        return;

    uint64_t b = (uint64_t) p - (uint64_t) heap;
    uint64_t o = (b % MAP_FACTOR) / BLK_SZ * MAP_ENTRY_SZ;
    b /= MAP_FACTOR;

    // If we were only ever changing a whole qword at time I think we'd be fine without turning of interrupts, but since each entry
    //   is only a couple bits, someone else may also want to change another part of a given qword at the same time.
    no_ints();
    for (uint64_t mask = 0b11ull << o;; b++, mask = 0b11ull, o = 0) {
        for (; mask && (map[b] & mask) == BPART << o; mask <<= 2, o += 2)
            map[b] &= ~mask;

        if ((map[b] & mask) == BEND << o) {
            map[b] &= ~mask;
            break;
        }
    }
    ints_okay();
}

static uint64_t blockCount(void* p) {
}

// Nope, actually there's no good way to zero out "new part" -- caller might consider themself to have, say, a 16-byte region, so
//   and there's no way with current setup for us to know when the caller's perspective is having less than a block.  We can't zero out
//   the correct area without knowing more than we can know without changing things in ways I don't want to, at least not now, maybe ever.
static void* dorealloc(void* p, int newSize, int zero) {
    if (heap == 0)
        return 0;

    // TODO:
    // I'm not 100% sure this is worth it, but it seems worth considering;
    //
    // 0. Walk old p to determine allocation size.
    // 1. If newSize and old size are both 1 block or less, simply return p.
    // 2. If newSize is less than or equal to old size, we can skip copying, and just mark the last bock as END and free the rest
    //    of the blocks, then return p.

    pbc = blockCount(p);
    if (pbc == 1 && newSize <= BLK_SZ) {
        if (zero) {
            // Zero out new part of that block of newSize
        }
        return p;
    }

    uint64_t* q1 = (uint64_t*) p;
    //uint64_t* q2 = zero ? mallocz(newSize) : malloc(newSize); // Inefficent but easy and good enough for now to zero whole thing
    uint64_t* q2 = malloc(newSize);
    uint8_t* b1 = (uint8_t*) p;
    uint8_t* b2 = (uint8_t*) q2;

    // TODO: With map, we now know how many blocks p is, so don't copy past end of it.
    //   And don't call mallocz above, but if newSize is larger than oldSize, fill new space with zeros if zero.

    int i, j;
    for (i = 0; i < newSize / 8; i++)
        q2[i] = q1[i];
    for (j = i*8; j < i + newSize % 8; j++)
        b2[j] = b1[j];

    free(p);
    return q2;
}

void* realloc(void* p, int newSize) {
    return dorealloc(p, newSize, 0);
}

void* reallocz(void* p, int newSize) {
    return dorealloc(p, newSize, 1);
}
