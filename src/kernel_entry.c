#include <stdint.h>
#include "console.h"

#define VRAM 0xb8000

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    //clearScreen();
    //print("Kernel launched!\n");
    //return;

    printColor("Running 64-bit kernel written in C!\n", 0x0d);

    //print("\n\n\n\n\n                  ");
    //printColor("Cool!", 0x3f);
    //for(int i=0; i<25; i++) print("0123456789abcdefghijklmnopqrstuvwxyz!\n");
    //print("Hi!\n");
    //print("01234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
    //print("01234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
}
