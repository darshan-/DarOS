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

    log("Kernel loaded; going to waitloop\n");
    waitloop();
}
