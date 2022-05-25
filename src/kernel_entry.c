#include <stdint.h>
#include "console.h"
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
    com1_print("\n\n\n{{Z}}\n\n\n");

    clearScreen();
    com1_print("\n\n\n{{Y}}\n\n\n");
    printColor("\3 Ready! \2\n", 0x0d);
    com1_print("\n\n\n{{X}}\n\n\n");

    com1_print("starting tty\n");
    startTty();

    // char demo[255];
    // for (uint8_t i = 0; i < 254; i++)
    //     demo[i] = (char) i+1;
    // print(demo);

    updateMemUse();
    unmask_pics();
    com1_print("going to waitloop\n");
    waitloop();
}
