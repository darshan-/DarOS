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

  Wait, almost.  Have the change the meaning slightly.

  What's the code for a single-block allocation?
  Do I use 11, and have that not imply continuation on either side, or use 01, and have that not imply 
    continuation to the right?

  I'm pretty down with 00 = free and 10 = region_end

  Hmm, 01 = region_with_more, 10 = region without more, like my original suggestion?
  That seems simple and good enough, and was my first thought.
  Otherwise you have five states (free, solitary, first, middle, last) -- you can use middle for solitary,
    but then you'd need to check both sides of it to see what's going on?
  I guess the advantage, and probably why I thought of this variation, is that you can start by using
    the mask of all 1s, and if there is more than 1 block in the region, turn off the first and last.
  The only change is from how I first wrote it (but not how I first meant it), *don't* take 11 to mean
    there's necessarily anything on either side.
  Hmm, I'd prefer it to be hard to accidentally free something in the middle of your region.
    free(block marked as first) frees the whole region
  Or
    free(block marked as middle or last) { doesn't free?  frees whole region if system makes that possible?)
  But definitely not
    free(block in the middle a region, with a system that means we can't know that, so we free it)


  Well, I *can* have a middle marker if I make the minimum region size 2 blocks, which is an intersting
    thought, but I think I'll go with just three states for now.


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

static inline void dumpAddr(char* name, uint64_t addr) {
    //return;
    char buf[17];
    buf[16] = 0;
    com1_print(name);
    com1_print(": ");
    qwordToHex(addr, buf);
    com1_print(buf);
    com1_print("\n");
}

#define dump(v) dumpAddr(#v, (uint64_t) (v))

#define BLK_SZ 128
#define MAP_ENTRY_SZ 2
#define START 0x0100000
#define BFREE 0b00
#define BPART 0b11
#define BEND  0b10

// Ooh, and that helps, right?
// Conceptually, the first bit means "in use" and the second bit means "region continues to the right",
//  although 0b01 would be treated as "taken, unknown", so not fully independent.
// Anyway, it means that transforming a search mask to a alloc mask just takes turning the last 1 off,
//  and I like that.

static uint64_t map_size; // Size in quadwords
static uint64_t* map = (uint64_t*) START;
//static uint64_t* heap = (uint64_t*) 0;
static uint64_t* heap = (uint64_t*) START;

// size is number of bytes available to us for (map + heap)
void init_heap(uint64_t size) {
    uint64_t factor = 64 / MAP_ENTRY_SZ * BLK_SZ;
    map_size = size / (1 + factor);
    for (uint64_t i = 0; i < map_size; i++)
        map[i] = 0;
    heap = map + map_size;
}

void* malloc(uint64_t nBytes) {
    com1_print("MALLOC ----- START\n");
    if (heap == map) return 0;
    if (nBytes > 64 / MAP_ENTRY_SZ * BLK_SZ) return 0;  // For now; later will support more than one qword per alloc

    uint64_t needed = (nBytes / BLK_SZ) + !!(nBytes % BLK_SZ);
    uint64_t mask = 0;

    dump(nBytes);
    dump(needed);
    dump(heap);

    for (uint64_t i = 0; i < needed; i++)
        mask = (mask << 2) + 0b11;

    dump(mask);

    for (uint64_t i = 0; i < map_size; i++) {
        uint64_t m = mask;
        for (uint64_t j = 0; j < 64 / MAP_ENTRY_SZ - (needed - 1); j++, m <<= 2) {
        //for (uint64_t j = 0; j < 64 - (needed - 1); j += 2, m <<= 2) {
            dump(m);
            dump(map[i]);
            if (!(map[i] & m)) {
                com1_print("found!\n");

                dump(i);
                dump(j);

                // So then this mask (m) will need to change
                // m should be transformed so that the first 1 becomes a 0, and if there's more than
                //   one 1 left, the last 1 becomes a 0?
                //   ...00011 -> ...0001
                // Let's transform m from a search mask to an allocation mask by turning off the last bit that's on
                // j * 2 should be the index of the rightmost on bit?
                // Hmm, let's rewrite it to keep using j as ones, but += 2 it in the loop...

                dump(m);
                // com1_print("Transforming m from search mask to allocation mask by turning off last bit\n");
                // m ^= 1 << (j * 2);
                //m ^= 1 << j;

                // Wait, little endian -- we want to set the leftmost entry to BEND, so turn off the *second*
                //   on bit... which we should be able to calculate based on needed and j...
                m ^= 1 << ((needed - 1 + j) * 2);
                dump(m);

                map[i] |= m;
                dump(map[i]);
                //void* ret = (void*) (uint64_t) heap + (i * (64 / MAP_ENTRY_SZ) + (j * 2)) * BLK_SZ;
                void* ret = (void*) (uint64_t) heap + (i * (64 / MAP_ENTRY_SZ) + j) * BLK_SZ;
                dump(ret);
                return ret;
            }
        }
    }

    return 0;
}

void free(void *p) {
    com1_print("  FREE  FREE  FREE  FREE  FREE  FREE  FREE  FREE ----- START\n");
    if (p < (void*) heap || p > (void*) heap + (map_size * (64 / MAP_ENTRY_SZ) - 1) * BLK_SZ)
        return;

    dump(p);
    // Figure map qword that holds the entry for p
    // Figure the bit pair for it
    // While that bit pair is BPART, set it to BFREE and increment so that we're pointing at either
    //   p if that wasn't BPART, or the first one after a BPART
    // If we're not pointing at BEND, someting is wrong.  Log it in a big way.
    // If it is BEND, set it to BFREE, and we're done!

    uint64_t b = (uint64_t) p; // 140
    b -= (uint64_t) heap;      // 128
    const uint64_t align = (64 / MAP_ENTRY_SZ) * BLK_SZ;
    uint64_t o = (b % align) / BLK_SZ * MAP_ENTRY_SZ;
    b /= align;

    com1_print("Okay, theoretically b is the offset into map for the qword we want?\n");
    dump(b);

    com1_print("======================And hopefully o is the offset into that that we want?\n");
    dump(o);

    // Okay, now we want to figure out which two bits to look at for the start (and perhaps entirety) of the region.
    //uint64_t m = 0b11 << o;
    // Which of the 32 locations do we want?  I guess the remainder of the division above?

    // Okay, that's done (above), should be good to check!

    //while (map[b]|

    // Hmm, wait, simplest to shift a copy of it while we proceed, in case of multi-block region...

    dump(map[b]);
    uint64_t entry = map[b];
    dump(entry);
    entry >>= o;
    dump(entry);
    uint64_t free_mask = ~(0b11 << o);
    dump(free_mask);
    uint64_t code = entry & 0b11;
    dump(code);

    if (code == BFREE) {
        com1_print("Ooooooooooooooooooops!  Page is already free...\n");
        return;
    }

    while (code == BPART) {
        // Huh, well, here it's easier if we hadn't been shifting the copy, but had a mask aligned for freeing...
        // Well, is it crazy to have that too, and shift it left as we shift entry right here?  Let's try...

        map[b] &= free_mask;
        dump(map[b]);
        entry >>= 2;
        dump(entry);
        free_mask <<= 2;
        dump(free_mask);
        code = entry & 0b11;
        dump(code);
    }

    if (code != BEND) {
        com1_print("Ooooooooooooooooooops!  Contiguous region didn't end with BEND, and we don't yet support multi-entry regions, so this shouldn't happen...\n");
        return;
    }

    map[b] &= free_mask;
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
