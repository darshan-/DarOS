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
        if (mem_table[i].start < 0x200000) {
            if (mem_table[i].start + mem_table[i].length < 0x200000) {
                mem_table[i].type = 2;
                continue;
            }

            mem_table[i].length -= 0x200000 - mem_table[i].start;
            mem_table[i].start = 0x200000;
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

    log("Kernel initialized; going to waitloop.\n");
    waitloop();
}
