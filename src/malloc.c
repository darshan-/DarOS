#include <stdint.h>
#include "malloc.h"
#include "serial.h"
#include "strings.h"

#define BLK_SZ 128
#define MAP_ENTRY_SZ 2
#define START 0x0100000
#define BFREE 0b00
#define BPART 0b11
#define BEND  0b10

static uint64_t map_size; // Size in quadwords
static uint64_t* map = (uint64_t*) START;
static uint64_t* heap = (uint64_t*) START;

static inline void binQword(uint64_t q) {
    for (uint64_t i = 0; i < 64; i++) {
        if (i != 0 && !(i % 4))
            com1_write(' ');
        com1_write(((q & (1ull << i)) >> i) + '0');
    }
    com1_write('\n');
}

void dumpEntries(int n) {
    for (int i = 0; i < n; i++)
        binQword(map[i]);
}

// `size' is the number of bytes available to us for (map + heap)
void init_heap(uint64_t size) {
    uint64_t factor = 64 / MAP_ENTRY_SZ * BLK_SZ;
    map_size = size / (1 + factor);
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

    return p * 128;
}

void* malloc(uint64_t nBytes) {
    com1_print("malloc");plumpPCs();
    if (heap == map) return 0;
    if (nBytes > 64 / MAP_ENTRY_SZ * BLK_SZ) return 0;  // For now; later will support more than one qword per alloc

    uint64_t needed = (nBytes / BLK_SZ) + !!(nBytes % BLK_SZ);
    uint64_t mask = 0;

    for (uint64_t i = 0; i < needed; i++)
        mask = (mask << 2) + 0b11;

    for (uint64_t i = 0; i < map_size; i++) {
        uint64_t m = mask;
        for (uint64_t j = 0; j < 64 / MAP_ENTRY_SZ - (needed - 1); j++, m <<= 2) {
            if (!(map[i] & m)) {
                m ^= 1ull << ((needed - 1 + j) * 2);

                map[i] |= m;
                void* ret = (void*) (uint64_t) heap + (i * (64 / MAP_ENTRY_SZ) + j) * BLK_SZ;

                return ret;
            }
        }
    }

    return 0;
}

void free(void *p) {
    com1_print("free");plumpPCs();
    if (p < (void*) heap || p > (void*) heap + (map_size * (64 / MAP_ENTRY_SZ) - 1) * BLK_SZ)
        return;

    uint64_t b = (uint64_t) p;
    b -= (uint64_t) heap;

#define align ((64 / MAP_ENTRY_SZ) * BLK_SZ)
    uint64_t o = (b % align) / BLK_SZ * MAP_ENTRY_SZ;
    b /= align;
#undef align

    uint64_t entry = map[b];
    entry >>= o;
    uint64_t free_mask = ~(0b11ull << o);
    uint64_t code = entry & 0b11;

    // TODO?  We *could* check that p is a starting block by making sure the block before is marked as either
    //  free or end of region.  I don't know if freeing a non-start block is a common error it would be helpful
    //  to guard against.  Oh, or, for that matter, p should be a multiple of 128...  We're not even checking
    //  that p is a block start address.  So, same deal: we can be more robust by checkign those things, but
    //  I don't know if it's worth guarding against.  But it could catch a bug every now and then, and it would
    //  be easy to do do...  So maybe?

    if (code == BFREE) {
        com1_print("------------------------------Ooooooooooooooooooops!  Page is already free...\n");
        return;
    }

    while (code == BPART) {
        map[b] &= free_mask;
        entry >>= 2;
        free_mask = ~((~free_mask) << 2);
        code = entry & 0b11;
    }

    if (code != BEND) {
        com1_print("------------------------------Ooooooooooooooooooops!  Contiguous region didn't end with BEND, and we don't yet support multi-entry regions, so this shouldn't happen...\n");
        return;
    }

    map[b] &= free_mask;
    com1_print("free END");plumpPCs();
}

void* realloc(void* p, int newSize) {
    com1_print("realloc");plumpPCs();
    if (heap == map) return 0;

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
