#include <stdint.h>

#include "acpi.h"
#include "console.h"
#include "hpet.h"
#include "interrupt.h"
#include "log.h"
#include "malloc.h"
#include "strings.h"

struct mem_table_entry {
    uint64_t start : 64;
    uint64_t length : 64;
    uint64_t type : 64;
};

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    // For now, let's use memory this way:
    // If the second largest usable region is large enough for the stack, make that the stack,
    //   otherwise put it at the start of the largest region.
    // Then the heap is the largest region, or what's left of it after putting the stack at the start of it.

    uint32_t* entry_count = (uint32_t*) 0x4000;
    uint64_t l1 = 0, l2 = 0;
    uint32_t i1, i2;
    struct mem_table_entry* mem_table = (struct mem_table_entry*) 0x4004;
    for (uint32_t i = 0; i < *entry_count; i++) {
        if (mem_table[i].type != 1)
            continue;

        if (mem_table[i].length > l1) {
            l2 = l1;
            i2 = i1;

            l1 = mem_table[i].length;
            i1 = i;
        }

        // logf("0x%p016h - 0x%p016h : %u\n", mem_table[i].start,
        //      mem_table[i].start + mem_table[i].length - 1, mem_table[i].type);
    }

    init_heap(100*1024*1024);
    init_interrupts();

    parse_acpi_tables();
    init_hpet();

    startTty();

    for (uint32_t i = 0; i < *entry_count; i++) {
        printf("0x%p016h - 0x%p016h : %u\n", mem_table[i].start,
               mem_table[i].start + mem_table[i].length - 1, mem_table[i].type);
    }

    printf("Most largest: 0x%p016h - 0x%p016h\n", mem_table[i1].start, l1 - 1);
    printf("Next largest: 0x%p016h - 0x%p016h\n", mem_table[i2].start, l2 - 1);

    log("Kernel loaded; going to waitloop\n");
    waitloop();
}
