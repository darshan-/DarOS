#include <stdint.h>

#include "malloc.h"

#include "interrupt.h"

#include "../lib/strings.h"

#define BLK_SZ 128
#define MAP_ENTRY_SZ 2
#define QBLK_SZ (64 / MAP_ENTRY_SZ * BLK_SZ)
#define L2_PAGE_SZ (2ull * 1024 * 1024)
#define heap64 ((uint64_t) heap)

#define BFREE 0b00ull
#define BPART 0b11ull
#define BEND  0b10ull

#define blocks_per(region_sz, block_sz) ((region_sz / block_sz) + !!(region_sz % block_sz))

static uint64_t map_size; // Size in quadwords
static uint64_t* map = (uint64_t*) 0;
static uint64_t* heap = (uint64_t*) 0;

// `size' is the number of bytes available to us for (map + heap)
// I think I want to 4096-align (0x1000) heap start, to make pages page aligned, to make palloc a bit easier
//   (so l2 2MB page alignment will be a whole number of our pages...)
void init_heap(uint64_t* start, uint64_t size) {
    map_size = size / (QBLK_SZ / 8 + 1) / 8;
    map = start;
    for (uint64_t i = 0; i < map_size; i++)
        map[i] = 0;
    heap = map + map_size;
    if (heap64 % 0x1000)
        heap = (void*) ((heap64 + 0x1000) & ~0xfffull);
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
    return map_size * QBLK_SZ;
}

void* malloc(uint64_t nBytes) {
    if (heap == 0 || nBytes == 0)
        return 0;

    uint64_t needed = blocks_per(nBytes, BLK_SZ);
    uint64_t mask = 0;

    for (uint64_t i = 0; i < needed && mask != -1ull; i++)
        mask = (mask << 2) + 0b11;

    uint64_t need;// = needed; // TODO: Looks like this was redundant, right?
    void* ret = 0;

    no_ints();
    for (uint64_t i = 0; i < map_size; i++) {
        for (need = needed; need > 64 / MAP_ENTRY_SZ; i++) {
            if (map[i])
                goto not_found;

            if (!ret)
                ret = (void*) heap + i * QBLK_SZ;

            need -= 64 / MAP_ENTRY_SZ;
        }

        mask = 0;
        for (uint64_t j = 0; j < need; j++)
            mask = (mask << 2) + 0b11;

        for (uint64_t j = 0; j < 64 / MAP_ENTRY_SZ - (need - 1); j++, mask <<= 2) {
            if (!(map[i] & mask)) {
                mask ^= 1ull << ((need - 1 + j) * 2);

                if (!ret)
                    ret = (void*) heap + (i * (64 / MAP_ENTRY_SZ) + j) * BLK_SZ;

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

    // heap64 % L2_PAGE_SZ is something...  Hmm, if we subtract that from a 512-map-qword-aligned...  Ugh, I think it's fine how we have it!
    uint64_t i = (((heap64 + map_size * QBLK_SZ) & ~(L2_PAGE_SZ - 1)) - L2_PAGE_SZ - heap64) / QBLK_SZ;

    no_ints();
    for (; i > 0; i -= L2_PAGE_SZ / QBLK_SZ) {
        for (uint64_t j = 0; j < 512; j++)
            if (map[i + j])
                goto next;

        for (uint64_t j = 0; j < 512; j++)
            map[i + j] = -1ull;

        map[i + 511] -= 1;

        ints_okay();
        return (void*) heap + i * QBLK_SZ;
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

    uint64_t n = (uint64_t) p - heap64;
    uint64_t o = (n % QBLK_SZ) / BLK_SZ * MAP_ENTRY_SZ;
    n /= QBLK_SZ;

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
    uint64_t n = (uint64_t) p - heap64;
    uint64_t o = (n % QBLK_SZ) / BLK_SZ * MAP_ENTRY_SZ;
    n /= QBLK_SZ;

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
