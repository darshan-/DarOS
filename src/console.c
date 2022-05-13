#include <stdint.h>
#include "console.h"

static uint8_t* cur = (uint8_t*) VRAM + (160*4);

void clearScreen() {
    for (uint64_t* v = (uint64_t*) VRAM; v < (uint64_t*) VRAM + 160/8*25; v++)
        *v = 0x0700070007000700;
    cur = (uint8_t*) VRAM;
}

static inline void advanceLine() {
    uint8_t* vram = (uint8_t*) VRAM;

    for (int i=0; i<24; i++)
        for (int j=0; j<160; j++)
            vram[i*160+j] = vram[(i+1)*160+j];

    for (uint64_t* v = (uint64_t*) VRAM + 160/8*24; v < (uint64_t*) VRAM + 160/8*25; v++)
         *v = 0x0700070007000700;
}

static inline void curAdvanced() {
    if (cur < (uint8_t*) VRAM + (160*25))
        return;

    advanceLine();
    cur = (uint8_t*) VRAM + (160*24);
}

static inline void printCharColor(uint8_t c, uint8_t color) {
    *cur++ = c;
    *cur++ = color;
    curAdvanced();
}

static inline void printChar(uint8_t c) {
    printCharColor(c, 0x07);
}

void printColor(char* s, uint8_t c) {
    while (*s != 0) {
        if (*s == '\n') {
            for (uint64_t n = 160 - ((uint64_t) cur - VRAM) % 160; n > 0; n -= 2)
                printCharColor(0, c);
            *s++;
        } else {
            printCharColor(*s++, c);
        }
    }
}

void print(char* s) {
    printColor(s, 0x07);
}

// void printConsoleFuncs() {
//     print("0x");
//     hexoutQword(printChar, printChar);
//     print("\n");
//     print("0x");
//     hexoutQword(print, printChar);
//     print("\n");
//     print("0x");
//     hexoutQword(ptr_printChar, printChar);
//     print("\n");
// }

// Have bottom line be a solid color background and have a clock and other status info?  (Or top line?)
//   Easy enough if this file supports it (with cur, clearScreen, and advanceLine (and printColor, if at bottom).
