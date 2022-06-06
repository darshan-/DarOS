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
    print("Hi, I'm C!  You know?\n");

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
    print("Mem table parsed.\n");

    kernel_stack_top = 0x160000;
    uint64_t heap = ((uint64_t) kernel_stack_top + 16) & ~0b1111ull;
    init_heap((uint64_t*) heap, 1*1024*1024);
    print("Heap initialized.\n");

    __asm__ __volatile__("sti");
    for (;;)
        __asm__ __volatile__("hlt");
    // no_ints();
    // clearScreen();
    // print("Hi, I'm C!  You know?\n");
    // __asm__ __volatile__("hlt");
    // For now, let's use memory this way:
    // If the second largest usable region is large enough for the stack, make that the stack,
    //   otherwise put it at the start of the largest region.
    // Then the heap is the largest region, or what's left of it after putting the stack at the start of it.

    // uint32_t* entry_count = (uint32_t*) 0x4000;
    // uint64_t largest = 0;
    // uint32_t il;
    // struct mem_table_entry* mem_table = (struct mem_table_entry*) 0x4004;
    // for (uint32_t i = 0; i < *entry_count; i++) {
    //     if (mem_table[i].type != 1)
    //         continue;

    //     if (mem_table[i].length > largest) {
    //         largest = mem_table[i].length;
    //         il = i;
    //     }
    // }
    // print("Mem table parsed.\n");

    //kernel_stack_top = (uint64_t*) ((mem_table[il].start + 64 * 1024) & ~0b1111ull);
    //kernel_stack_top = 0x160000;
    //print("kernel stack set; will be used when we next start waitloop.\n");
    //__asm__ __volatile__("hlt");
    //uint64_t heap = ((uint64_t) kernel_stack_top + 16) & ~0b1111ull;

    //init_heap((uint64_t*) heap, mem_table[il].length - (heap - mem_table[il].start));
    //init_heap((uint64_t*) heap, 1*1024*1024);
    // init_heap((uint64_t*) 0x160000, 1*1024*1024);
    // print("Heap initialized.\n");
    printf("We've got %u entries:\n", *entry_count);
    // for (uint32_t i = 0; i < *entry_count; i++) {
    //     printf("0x%p016h - 0x%p016h : %u\n", mem_table[i].start,
    //            mem_table[i].start + mem_table[i].length - 1, mem_table[i].type);
    //     if (i >= 11)
    //         break;
    //         //__asm__ __volatile__("hlt");
    // }

    // __asm__ __volatile__("hlt");
    printf("Largest: 0x%p016h - 0x%p016h\n", mem_table[il].start, mem_table[il].start + largest - 1);
    //__asm__ __volatile__("hlt");

    printf("kernel_stack_top: 0x%p016h\n", kernel_stack_top);
    //__asm__ __volatile__("hlt");
    printf("heap: 0x%p016h\n", heap);
    printf("heap size: %u MB\n", (mem_table[il].length - (heap - mem_table[il].start)) / 1024 / 1024);

    init_interrupts();
    //parse_acpi_tables();
    //print("Parsed ACPI tables.\n");
    //__asm__ __volatile__("hlt");
    //init_hpet();
    //print("Started initializing HPET.\n");

    //__asm__ __volatile__("hlt");
    startTty();

    print("Started tty.\n");

    log("Kernel loaded; going to waitloop\n");
    waitloop();
}
