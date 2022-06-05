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
    no_ints();
    clearScreen();
    print("Hi, I'm C!");
    __asm__ __volatile__("hlt");
    // For now, let's use memory this way:
    // If the second largest usable region is large enough for the stack, make that the stack,
    //   otherwise put it at the start of the largest region.
    // Then the heap is the largest region, or what's left of it after putting the stack at the start of it.

    uint32_t* entry_count = (uint32_t*) 0x4000;
    uint64_t largest = 0;
    uint32_t il;
    struct mem_table_entry* mem_table = (struct mem_table_entry*) 0x4004;
    for (uint32_t i = 0; i < *entry_count; i++) {
        if (mem_table[i].type != 1)
            continue;

        if (mem_table[i].length > largest) {
            largest = mem_table[i].length;
            il = i;
        }
    }

    kernel_stack_top = (uint64_t*) ((mem_table[il].start + 64 * 1024) & ~0b1111);
    uint64_t heap = ((uint64_t) kernel_stack_top + 16) & ~0b1111;

    //init_heap((uint64_t*) heap, mem_table[il].length - (heap - mem_table[il].start));
    init_heap((uint64_t*) heap, 100*1024*1024);
    init_interrupts();
    parse_acpi_tables();
    init_hpet();

    startTty();

    for (uint32_t i = 0; i < *entry_count; i++) {
        printf("0x%p016h - 0x%p016h : %u\n", mem_table[i].start,
               mem_table[i].start + mem_table[i].length - 1, mem_table[i].type);
    }

    printf("Largest: 0x%p016h - 0x%p016h\n", mem_table[il].start, largest - 1);

    printf("kernel_stack_top: 0x%p016h\n", kernel_stack_top);
    printf("heap: 0x%p016h\n", heap);
    printf("heap size: %u MB\n", (mem_table[il].length - (heap - mem_table[il].start)) / 1024 / 1024);

    log("Kernel loaded; going to waitloop\n");
    waitloop();
}
