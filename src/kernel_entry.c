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
    init_heap(100*1024*1024);
    init_interrupts();

    parse_acpi_tables();
    init_hpet();

    startTty();

    uint32_t* entry_count = (uint32_t*) 0x4000;
    struct mem_table_entry* mem_table = (struct mem_table_entry*) 0x4004;
    for (uint32_t i = 0; i < *entry_count; i++)
        logf("0x%p016h - 0x%p016h : %u\n", mem_table[i].start, mem_table[i].start + mem_table[i].length - 1, mem_table[i].type);

    log("Kernel loaded; going to waitloop\n");
    waitloop();
}
