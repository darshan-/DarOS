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

#define STACK_SIZE (64 * 1024)

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
    startTty();

    printf("We've got %u entries:\n", *entry_count);
    for (uint32_t i = 0; i < *entry_count; i++) {
        printf("0x%p016h - 0x%p016h : %u\n", mem_table[i].start,
               mem_table[i].start + mem_table[i].length - 1, mem_table[i].type);
    }
    printf("Largest: 0x%p016h - 0x%p016h\n", mem_table[il].start, mem_table[il].start + largest - 1);
    printf("Stack top / heap bottom: 0x%h\n", kernel_stack_top);
    printf("Heap is %u MB.\n", heapSize() / 1024 / 1024);

    waitloop();

    // no_ints();
    // clearScreen();
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

    // __asm__ __volatile__("hlt");
    //__asm__ __volatile__("hlt");

    // printf("kernel_stack_top: 0x%p016h\n", kernel_stack_top);
    // //__asm__ __volatile__("hlt");
    // printf("heap: 0x%p016h\n", heap);
    // printf("heap size: %u MB\n", (mem_table[il].length - (heap - mem_table[il].start)) / 1024 / 1024);

    // init_interrupts();
    // //parse_acpi_tables();
    // //print("Parsed ACPI tables.\n");
    // //__asm__ __volatile__("hlt");
    // //init_hpet();
    // //print("Started initializing HPET.\n");

    // //__asm__ __volatile__("hlt");
    // startTty();

    // print("Started tty.\n");

    // log("Kernel loaded; going to waitloop\n");
    // waitloop();
}
