#include <stdint.h>

#include "malloc.h"

#define BLK_SZ 128
#define MAP_ENTRY_SZ 2
#define QBLK_SZ (64 / MAP_ENTRY_SZ * BLK_SZ)
#define heap64 ((uint64_t) heap)

#define BFREE 0b00ull
#define BPART 0b11ull
#define BEND  0b10ull

#define blocks_per(region_sz, block_sz) ((region_sz / block_sz) + !!(region_sz % block_sz))

static uint64_t map_size; // Size in quadwords
static uint64_t* map = (uint64_t*) 0x7FC0180000ull;
static uint64_t* heap = (uint64_t*) 0;

static void init_heap() {
    map_size = (512 * 1024) / (QBLK_SZ / 8 + 1) / 8;
    for (uint64_t i = 0; i < map_size; i++)
        map[i] = 0;
    heap = map + map_size;
    if (heap64 % 0x1000)
        heap = (void*) ((heap64 + 0x1000) & ~0xfffull);
}

void* malloc(uint64_t nBytes) {
    if (nBytes == 0)
        return 0;

    if (heap == 0)
        init_heap();

    uint64_t needed = blocks_per(nBytes, BLK_SZ);
    uint64_t mask = 0;

    for (uint64_t i = 0; i < needed && mask != -1ull; i++)
        mask = (mask << 2) + 0b11;

    uint64_t need;
    void* ret = 0;

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

                return ret;
            } else if (needed > 64 / MAP_ENTRY_SZ) {
                goto not_found;
            }
        }

    not_found:
        need = needed;
        ret = 0;
    }

    return 0;
}

void free(void *p) {
    if (heap == 0 || p < (void*) heap || p > (void*) heap + (map_size * (64 / MAP_ENTRY_SZ) - 1) * BLK_SZ)
        return;

    uint64_t n = (uint64_t) p - heap64;
    uint64_t o = (n % QBLK_SZ) / BLK_SZ * MAP_ENTRY_SZ;
    n /= QBLK_SZ;

    for (uint64_t mask = 0b11ull << o;; n++, mask = 0b11ull, o = 0) {
        for (; mask && (map[n] & mask) == BPART << o; mask <<= 2, o += 2)
            map[n] &= ~mask;

        if ((map[n] & mask) == BEND << o) {
            map[n] &= ~mask;
            break;
        }
    }
}
