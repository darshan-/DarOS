#include <stdint.h>

#include "malloc.h"

#include "interrupt.h"

#include "log.h"
#include "strings.h"

#define BLK_SZ 128
#define MAP_ENTRY_SZ 2
#define MAP_FACTOR (64 / MAP_ENTRY_SZ * BLK_SZ)
#define L2_PAGE_SZ (2ull * 1024 * 1024)
#define BFREE 0b00ull
#define BPART 0b11ull
#define BEND  0b10ull

#define blocks_per(region_sz, block_sz) ((region_sz / block_sz) + !!(region_sz % block_sz))

static uint64_t map_size; // Size in quadwords
static uint64_t* map = (uint64_t*) 0;
static uint64_t* heap = (uint64_t*) 0;

// Okay, I worded "factor" wrong!  1 qword gets us 4096 bytes, so that's not the factor, that's mem size per qword.
// Factor is an eighth of that.
// TODO: 1. Rename MAP_FACTOR to something conveying PG_SZ_PER_MAP_QWORD -- but hopefully shorter and better!
//       2. Have MAP_FACTOR be a thing, but be the actual factor?

// `size' is the number of bytes available to us for (map + heap)
// I think I want to 4096-align (0x1000) heap start, to make pages page aligned, to make palloc a bit easier
//   (so l2 2MB page alignment will be a whole number of our pages...)
void init_heap(uint64_t* start, uint64_t size) {
    //map_size = size / 513 / 8;
    map_size = size / (8 / MAP_ENTRY_SZ * BLK_SZ + 1) / 8;
    map = start;
    for (uint64_t i = 0; i < map_size; i++)
        map[i] = 0;
    heap = map + map_size;
    if ((uint64_t) heap % 0x1000)
        heap = (void*) (((uint64_t) heap + 0x1000) & ~0xfffull);
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

// Number of bytes in the actual heap (not counting map of heap as part of heap)
uint64_t heapSize() {
    return map_size * MAP_FACTOR;
}

void* malloc(uint64_t nBytes) {
    if (heap == 0)
        return 0;

    uint64_t needed = blocks_per(nBytes, BLK_SZ);
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
                ret = (void*) (uint64_t) heap + i * MAP_FACTOR;

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

void* palloc() {
    if (heap == 0)
        return 0;

    uint64_t addr = ((uint64_t) heap + (map_size - 1) * MAP_FACTOR) & ~(L2_PAGE_SZ - 1);

    no_ints();
    for (uint64_t i = ((uint64_t) addr - (uint64_t) heap - L2_PAGE_SZ) / MAP_FACTOR; i > 0; i -= L2_PAGE_SZ / MAP_FACTOR) {
        for (uint64_t j = 0; j < 512; j++)
            if (map[i + j])
                goto next;

        for (uint64_t j = 0; j < 512; j++)
            map[i + j] = -1ull;

        map[i + 511] -= 1;

        ints_okay();
        return (void*) heap + i * MAP_FACTOR;
    next:
    }
    ints_okay();
    return 0;
}

void* mallocz(uint64_t nBytes) {
    uint64_t* p = malloc(nBytes);
    if (!p)
        return 0;

    for (uint64_t i = 0; i < blocks_per(nBytes, 8); i++)
        p[i] = 0;

    return p;
}

void free(void *p) {
    if (heap == 0 || p < (void*) heap || p > (void*) heap + (map_size * (64 / MAP_ENTRY_SZ) - 1) * BLK_SZ)
        return;

    uint64_t n = (uint64_t) p - (uint64_t) heap;
    uint64_t o = (n % MAP_FACTOR) / BLK_SZ * MAP_ENTRY_SZ;
    n /= MAP_FACTOR;

    // If we were only ever changing a whole qword at time I think we'd be fine without turning of interrupts, but since each entry
    //   is only a couple bits, someone else may also want to change another part of a given qword at the same time.
    no_ints();
    for (uint64_t mask = 0b11ull << o;; n++, mask = 0b11ull, o = 0) {
        for (; mask && (map[n] & mask) == BPART << o; mask <<= 2, o += 2)
            map[n] &= ~mask;

        if ((map[n] & mask) == BEND << o) {
            map[n] &= ~mask;
            break;
        }
    }
    ints_okay();
}

static void* dorealloc(void* p, uint64_t newSize, int zero) {
    if (heap == 0 || p < (void*) heap || p > (void*) heap + (map_size * (64 / MAP_ENTRY_SZ) - 1) * BLK_SZ)
        return 0;

    uint64_t nbc = blocks_per(newSize, BLK_SZ);
    uint64_t n = (uint64_t) p - (uint64_t) heap;
    uint64_t o = (n % MAP_FACTOR) / BLK_SZ * MAP_ENTRY_SZ;
    n /= MAP_FACTOR;

    uint64_t count = 0;
    uint8_t freeing = 0;
    no_ints();
    for (uint64_t mask = 0b11ull << o;; n++, mask = 0b11ull, o = 0) {
        for (; mask && (map[n] & mask) == BPART << o; mask <<= MAP_ENTRY_SZ, o += MAP_ENTRY_SZ) {
            count++;

            if (freeing) {
                map[n] &= ~mask;
            } else if (count == nbc) {
                freeing = 1;
                map[n] &= ~(1ull << o);
            }
        }

        if (mask && (map[n] & mask) == BEND << o) {
            count++;

            if (freeing) {
                map[n] &= ~mask;
            } else if (count != nbc) {
                uint64_t* q = malloc(newSize);

                uint64_t i;
                for (i = 0; i < count * BLK_SZ / 8; i++)
                    q[i] = ((uint64_t*) p)[i];
                if (zero)
                    for (; i < newSize / 8; i++)
                        q[i] = 0;

                free(p);
                p = q;
            }

            break;
        }
    }

    ints_okay();
    return p;
}

void* realloc(void* p, uint64_t newSize) {
    return dorealloc(p, newSize, 0);
}

// Only valid for regions that were allocated with allocz (and haven't used realloc (without the z)).  (It doesn't matter for regions
//   that are a whole number multiple of block size, but caller generally shouldn't know or care about BLK_SZ, and should stick to
//   this guideline.  And it doesn't matter for shrinking regions, but that's because it's equivalent to realloc in that case...)
void* reallocz(void* p, uint64_t newSize) {
    return dorealloc(p, newSize, 1);
}
