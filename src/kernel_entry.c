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

void mTest() {
    // com1_print("Entries before malloc():\n");
    // dumpEntries(8);
    void* p1 = malloc(1);
    void* p2 = malloc(1024);
    void* p3 = malloc(512);
    void* p4 = malloc(2048);
    void* p5 = malloc(2048);
    void* p6 = malloc(4096);
    void* p7 = malloc(4096);
    void* p8 = malloc(40960);
    // com1_print("Entries after malloc():\n");
    // dumpEntries(8);
    // printf("mem use: %u\n", memUsed());
    //com1_print("Well, that's what that was!\n");
    com1_printf("memUsed after malloc: %u\n", memUsed());
    free(p3);
    free(p2);
    free(p1);
    free(p5);
    free(p4);
    free(p6);
    free(p7);
    free(p8);
    com1_printf("memUsed after free: %u\n", memUsed());
    // com1_print("Entries after free():\n");
    // dumpEntries(8);
}

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

    mTest();
    log("Kernel initialized; going to waitloop.\n");
    waitloop();
}
