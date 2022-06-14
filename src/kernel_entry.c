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
    com1_printf("Heap used before malloc: %u\n", heapUsed());
    void* p1 = malloc(1);
    void* p2 = malloc(1024);
    void* p3 = malloc(512);
    void* p4 = malloc(2048);
    void* p5 = malloc(2048);
    void* p6 = malloc(4096);
    void* p7 = malloc(4096);
    void* p8 = malloc(40960);
    com1_printf("Heap used after malloc: %u\n", heapUsed());

    void* p1a = realloc(p1, 128);
    if (p1a != p1)
        com1_printf("Whoops!  0x%h != 0x%h\n", p1a, p1);
    void* p2a = realloc(p2, 1000);
    if (p2a != p2)
        com1_printf("Whoops!  0x%h != 0x%h\n", p2a, p2);
    void* p3a = realloc(p3, 510);
    if (p3a != p3)
        com1_printf("Whoops!  0x%h != 0x%h\n", p3a, p3);
    com1_printf("Heap used after static realloc: %u\n", heapUsed());

    p1 = realloc(p1, 129);
    if (p1 == p1a)
        com1_printf("Unlikely to be valid!  0x%h == 0x%h\n", p1a, p1);
    p2 = realloc(p2, 1025);
    if (p2 == p2a)
        com1_printf("Unlikely to be valid!  0x%h == 0x%h\n", p2a, p2);
    p3 = realloc(p3, 10250);
    if (p3 == p3a)
        com1_printf("Unlikely to be valid!  0x%h == 0x%h\n", p3a, p3);
    com1_printf("Heap used after growing realloc: %u\n", heapUsed());

    p1a = realloc(p1, 128);
    if (p1a != p1)
        com1_printf("Whoops!  0x%h != 0x%h\n", p1a, p1);
    p2a = realloc(p2, 1024);
    if (p2a != p2)
        com1_printf("Whoops!  0x%h != 0x%h\n", p2a, p2);
    p3a = realloc(p3, 510);
    if (p3a != p3)
        com1_printf("Whoops!  0x%h != 0x%h\n", p3a, p3);
    com1_printf("Heap used after shrinking realloc: %u\n", heapUsed());

    // TODO: Test mallocz and reallocz
    uint64_t* z1 = mallocz(1024);
    for (int i = 0; i < 1024 / 8; i++)
        if (z1[i] != 0)
            com1_print("non-zero content in region returned from mallocz!\n");
    z1 = reallocz(z1, 1);
    z1 = reallocz(z1, 4096);
    for (int i = 0; i < 4096 / 8; i++)
        if (z1[i] != 0)
            com1_print("non-zero content in region returned from mallocz!\n");
    free(z1);

    free(p3);
    free(p2);
    free(p1);
    free(p5);
    free(p4);
    free(p6);
    free(p7);
    free(p8);
    com1_printf("Heap used after free: %u\n", heapUsed());
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
