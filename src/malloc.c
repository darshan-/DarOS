#include <stdint.h>
#include "malloc.h"
#include "strings.h"


/*
  Hmm, perhaps let's have bocks of 128 bytes?
  (/ (* 9 1024 1024) 128) 73728 blocks
  So a bit field to indicate usage (0 = unused, 1 = used) is 1 KB per MB.

  How terrible would it be searching that bitfield for large contiguous regions?
  I mean, we can scan 64 at a time if we're on a boundary of 64 (compare a uint64_t).
  So 8 MB regions aren't too hard if fragmentation isn't bad.

  I'm sure there are way, way better algorithms, but I'm just looking for incremental
  improvements with a perference for simple -- so I can really appreciate the better
  approaches later by having gotten my hands dirty with dead simple approaches.

  I *think* it's a reasonable approach, and a real improvement over the current approach of
  just incrementing cur and never reclaiming any freed memory!

  So let's increase the heap to 100 MB rather than 9,
  (/ (* 100 1024 1024) 128)

  Hmm, or I can have kernel initialization pass in an amount of memory to use for the heap+map, and
  we'll work out how large the map should be to have a map and heap in that amount of space.  Yep,
  let's do it that way!
 */

#define BLK_SZ 128
#define START 0x0100000

static uint64_t map_size; // Size in quadwords
static uint64_t* map = (uint64_t*) START;
//static uint64_t* heap = (uint64_t*) 0;
static uint64_t* heap = (uint64_t*) START;

// size is number of bytes available to us for (map + heap)
void init_heap(uint64_t size) {
    uint64_t factor = 64 * BLK_SZ;
    map_size = size / (1 + factor);
    for (uint64_t i = 0; i < map_size; i++)
        map[i] = 0;
    heap = map + map_size;


    char buf[17];
    buf[16] = 0;

    com1_print("malloc inited\n");
    com1_print("factor: ");
    qwordToHex(factor, buf);
    com1_print(buf);
    com1_print("\n");
    
    com1_print("map_size: ");
    qwordToHex(map_size, buf);
    com1_print(buf);
    com1_print("\n");

    com1_print("heap: ");
    qwordToHex(heap, buf);
    com1_print(buf);
    com1_print("\n");
}

void* malloc(uint64_t nBytes) {
    com1_print("MALLOC ----- 1\n");
    if (heap == map) return 0;
    com1_print("MALLOC ----- 2\n");
    if (nBytes > 64 * BLK_SZ) return 0;  // Don't support more than 64 pages per call right now
    com1_print("MALLOC ----- 3\n");

    uint64_t needed = (nBytes / BLK_SZ) + !!(nBytes % BLK_SZ);
    uint64_t mask = 0;

    char buf[17];
    buf[16] = 0;

    com1_print("nBytes: ");
    qwordToHex(nBytes, buf);
    com1_print(buf);
    com1_print("\n");

    com1_print("needed: ");
    qwordToHex(needed, buf);
    com1_print(buf);
    com1_print("\n");

    com1_print("mask: ");
    qwordToHex(mask, buf);
    com1_print(buf);
    com1_print("\n");

    for (uint64_t i = 0; i < needed; i++)
        mask = (mask << 1) + 1;

    com1_print("mask: ");
    qwordToHex(mask, buf);
    com1_print(buf);
    com1_print("\n");

    for (uint64_t i = 0; i < map_size; i++) {
        uint64_t m = mask;
        for (uint64_t j = 0; j < 64 - needed + 1; j++, m <<= 1) {
            //m <<= 1;
            com1_print("   m: ");
            qwordToHex(m, buf);
            com1_print(buf);
            com1_print("\n");

            com1_print("mapi: ");
            qwordToHex(map[i], buf);
            com1_print(buf);
            com1_print("\n");
            if (!(map[i] & m)) {
                map[i] |= m;
            com1_print("mapi: ");
            qwordToHex(map[i], buf);
            com1_print(buf);
            com1_print("\n");
                void* ret = (void*) (uint64_t) heap + i * 64 * BLK_SZ + j;
                com1_print(" ret: ");
                qwordToHex(ret, buf);
                com1_print(buf);
                com1_print("\n");
                return ret;
                //return (void*) (uint64_t) heap + i * 64 * BLK_SZ + j;
            }
        }
    }

    return 0;
}

void free(void *p) {
    if (p < (void*) heap || p > (void*) heap + (map_size * 64 - 1) * BLK_SZ)
        return;
}

void* realloc(void* p, int newSize) {
    //if (!heap) return 0;
    com1_print("REALLOC ----- 1\n");
    if (heap == map) return 0;
    com1_print("REALLOC ----- 2\n");

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
