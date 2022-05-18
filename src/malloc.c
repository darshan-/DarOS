#include <stdint.h>
#include "malloc.h"
#include "strings.h"


/*

  Ah, and first thing that popped into my head while making some coffee after getting some sleep
  is that this doesn't work -- we don't know how many blocks to free in free()!  We either need
  more than 1 bit per block in the map, or to keep an extra structure (a dynamic one) for tracking
  allocations that are more than 1 block.

  Hmm, let's see, can we do it with just 2 bits per allocation?  That's 4 states, which seems likely
  workable?  Yeah, I think three is enough, although it might be helpful to use the 4th?

  00 : block is free
  01 : block is allocated and is part of a contiguous allocation region that continues further
  10 : block is allocated and is the end of a contiguous region

  That would be enough, right?  But I can think of at least one way to use the 4th bit that might be useful:

  00 : block is free
  01 : block is the first in a contiguous region that continues to the right (higher addresses)
  11 : block is part of a contiguous region with at least one block on each side
  10 : block is the last in a contiguous region (which is to the left / lower addresses)

  Since this is all in-memory, it seems like it would be fine to change my system at any time.


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

static inline void dumpAddr(char* name, void* addr) {
    //return;
    char buf[17];
    buf[16] = 0;
    com1_print(name);
    com1_print(": ");
    qwordToHex(addr, buf);
    com1_print(buf);
    com1_print("\n");
}

#define dump(v) dumpAddr(#v, v)

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
}

void* malloc(uint64_t nBytes) {
    com1_print("MALLOC ----- START\n");
    if (heap == map) return 0;
    if (nBytes > 32 * BLK_SZ) return 0;  // Don't support more than 32 pages per call right now

    uint64_t needed = (nBytes / BLK_SZ) + !!(nBytes % BLK_SZ);
    uint64_t mask = 0;

    dump(nBytes);
    dump(needed);

    for (uint64_t i = 0; i < needed; i++)
        mask = (mask << 1) + 1;

    dump(mask);

    for (uint64_t i = 0; i < map_size; i++) {
        uint64_t m = mask;
        for (uint64_t j = 0; j < 64 - needed + 1; j++, m <<= 1) {
            dump(m);
            dump(map[i]);
            if (!(map[i] & m)) {
                com1_print("found!\n");

                dump(i);
                dump(j);

                map[i] |= m;
                dump(map[i]);
                void* ret = (void*) (uint64_t) heap + (i * 64 + j) * BLK_SZ;
                dump(ret);
                return ret;
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

    // With 2 bits per block in the map, we'll know how may blocks p is, so we'll be good.

    int i, j;
    for (i = 0; i < newSize / 8; i++)
        q2[i] = q1[i];
    for (j = i*8; j < i + newSize % 8; j++)
        b2[j] = b1[j];

    free(p);
    return q2;
}
