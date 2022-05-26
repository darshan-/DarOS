#include <stdint.h>

#include "acpi.h"
#include "console.h"
#include "cpuid.h"
#include "hpet.h"
#include "interrupt.h"
#include "keyboard.h"
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
    printColor("Ready!\n", 0x0d);

    com1_print("starting tty\n");
    startTty();

    // char demo[255];
    // for (uint8_t i = 0; i < 254; i++)
    //     demo[i] = (char) i+1;
    // print(demo);

    updateMemUse();


    read_rsdp();
    //init_hpet(get_hpet_block());
    init_hpet();

    com1_print("going to waitloop\n");
    waitloop();
}
