#include <stdint.h>

#include "acpi.h"
#include "console.h"
#include "cpuid.h"
#include "hpet.h"
#include "interrupt.h"
#include "keyboard.h"
#include "log.h"
#include "malloc.h"
#include "serial.h"
#include "strings.h"

static void gotChar(char c) {
    printc(c);
}

static void startTty() {
    registerKbdListener(&gotChar);
};

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    init_heap(100*1024*1024);
    init_interrupts();

    clearScreen();

    log("starting tty\n");
    startTty();

    updateMemUse();

    parse_acpi_tables();
    init_hpet();

    log("going to waitloop\n");
    printColor("Ready!\n", 0x0d);
    waitloop();
}
