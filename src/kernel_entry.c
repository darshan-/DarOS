#include <stdint.h>
#include "console.h"
#include "interrupt.h"
#include "keyboard.h"
#include "malloc.h"
#include "rtc.h"
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
    init_idt();
    init_rtc();

    clearScreen();
    printColor("Ready!\n", 0x0d);

    com1_print("starting tty\n");
    startTty();

    updateMemUse();
    com1_print("going to waitloop\n");
    waitloop();
}
