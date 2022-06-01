#include <stdint.h>

#include "acpi.h"
#include "console.h"
#include "hpet.h"
#include "interrupt.h"
#include "log.h"
#include "malloc.h"
#include "strings.h"

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    init_heap(100*1024*1024);
    init_interrupts();

    parse_acpi_tables();
    init_hpet();

    startTty();

    uint32_t* entry_count = (uint32_t*) 0x4000;
    uint64_t* int_15_mem_table = (uint64_t*) 0x4004;
    for (uint32_t j = 0; j < 3 * *entry_count; j++)
        printf("0x%p016h\n", int_15_mem_table[j]);

    log("Kernel loaded; going to waitloop\n");
    waitloop();
}
