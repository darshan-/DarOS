#include <stdint.h>

#include "acpi.h"
#include "console.h"
#include "hpet.h"
#include "interrupt.h"
#include "log.h"
#include "malloc.h"
#include "serial.h"
#include "strings.h"

struct mem_table_entry {
    uint64_t start : 64;
    uint64_t length : 64;
    uint64_t type : 64;
};

#define STACK_SIZE (64 * 1024)

/*
  Overall to-do list:

  * User space
    - Need a user-mode segment in GDT, but I've been conflating GDT and page tables -- set new l4 table in cr3 as part of
      switch to user mode.  Not new GDT, but new l4 table.  Not new ldtr, but new cr3.
      We need to set the U/S bit (1<<2) on all three levels.
      We only make entries for valid addresses.  So first attempt would pick a single 2MB of memory (2MB-aligned) to be for
        my single user-space task, and wherever we put that, we... Oh, the point is paging.  So we can put that as the first
        and single entry of an l2 table, right?  And then the process will see itself as having flat memory from 0 to 2MB, and
        the entry tells the processsor where it is in physical memory.
  * Shell
  * HPET -- not just for finer resolution of timer, but as source of actual timer count -- missed ticks are less bad.
  * Network?
  * File system?
  * Graphics? (At least for my own text mode -- better/more colors, custom font, etc.?)

 */

/*

  Okay, so to set up a single user-mode page, I need 3 4k pages for tables, plus the 2 MB page itself.
  The user-mode code needs to be either entirely local references, or know where it's going to end up.
  Hmm, for this first experiment, trying for simplest and easiest test possible (well, that I can easily think of), I should be
    able to have it share the same identity-mapped address space.  It's just that they're using different page tables, and the
    user-space page tables a) are marked with u/s bit, and b) only have one page identity mapped -- the other should be null
    mapped.  (Mapping all other pages to the null descriptor only occured to me while writing the last sentence, and seems
    obviously what I'm meant to do, I think.)  Null-mapping means I flesh out all of l4 and l3 with null descriptors too, not
    just l2.  But all zeros makes a null-descriptor, I think, so getting mallocz to give my arbitrarily aligned memory (or just
    malloczing a region twice as large as I want to start at the aligned place myself for now) should be a workable approach for
    this first test.

  Yeah, AMD manual says "When the P bit is 0, indicating a not-present page, all remaining bits in the page data-structure entry
    are available to software," which seems to mean I can have the whole thing be zeroed, and that's valid, and means a fault will
    occur if user-mode code tries to access memory for that page.

 */

/*
  Okay, loop without hlt just runs forever; interrupts don't interupt.
  But with halt, bochs gives me this:
   interrupt(long mode): IDT entry extended attributes DWORD4 TYPE != 0
   interrupt(long mode): gate descriptor is not valid sys seg
  So, even though thing work from from ring 0, something isn't right, and this is a clue.
  interrupt stack table?
  what's with dword not being 0?
  Is it seeing a different idt?
  It is mapped; first 2 GB are identity mapped...

  Hmm, and disappointing, confusing, and kinda surprising more broadly, but also *not* really disappointing, and actually
    kinda good news given the above -- making the waitloop be busy too, it also can't be interrupted!  I totally thought it
    could!

  So maybe it's two separate issues, and recognizing that and hopefully solving both without too much trouble, we'll be in
    pretty good shape!  I had an issue I didn't know about before, and it's great to know and hopefully fix it soon: make
    interrupts work even when a stream of execution never halts.  And now that I know that, it's maybe less surprising and
    also maybe not so hard to track down and sort out, that something is up with the idt from user mode.

  Okay, so that's progress, for sure, to figure that much out!

  Hmm, now I can't reproduce it...  Did I accidentally comment out sti along with hlt last night?  I was sooooo tired...
 */

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    uint32_t* entry_count = (uint32_t*) 0x4000;
    uint64_t largest = 0;
    uint32_t il;
    struct mem_table_entry* mem_table = (struct mem_table_entry*) 0x4004;
    for (uint32_t i = 0; i < *entry_count; i++) {
        if (mem_table[i].type != 1)
            continue;

        // L2 pages mapping first 256 GB of address space take up the MB from 0x100000 to 0x200000
        // At some point I'll probably want to make that part of this process, but for now it's fixed there,
        //   and we'll just account for it here.
        if (mem_table[i].start < 0x300000) {
            if (mem_table[i].start + mem_table[i].length < 0x300000) {
                mem_table[i].type = 2;
                continue;
            }

            mem_table[i].length -= 0x300000 - mem_table[i].start;
            mem_table[i].start = 0x300000;
        }

        if (mem_table[i].length > largest) {
            largest = mem_table[i].length;
            il = i;
        }
    }

    kernel_stack_top = (uint64_t*) ((mem_table[il].start + STACK_SIZE) & ~0b1111ull);
    init_heap(kernel_stack_top, mem_table[il].length - STACK_SIZE);

    init_interrupts();
    init_com1();
    no_ints();
    startTty();

    logf("We've got %u mem table entries:\n", *entry_count);
    for (uint32_t i = 0; i < *entry_count; i++) {
        logf("0x%p016h - 0x%p016h : %u\n", mem_table[i].start,
               mem_table[i].start + mem_table[i].length - 1, mem_table[i].type);
    }
    logf("Largest memory region: 0x%p016h - 0x%p016h\n", mem_table[il].start, mem_table[il].start + largest - 1);
    logf("Stack top / heap bottom: 0x%h\n", kernel_stack_top);
    logf("Heap is %u MB.\n", heapSize() / 1024 / 1024);
    parse_acpi_tables();
    init_hpet();

    extern void* tss;
    *((void**) (tss + 4)) = kernel_stack_top;

    //setUpUserMode();
    logf("Set up heap with 0x%h, %u\n", kernel_stack_top, mem_table[il].length - STACK_SIZE);

    log("Kernel initialized; going to waitloop.\n");
    //um_r15();
    waitloop();
}
