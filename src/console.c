#include <stdint.h>
#include "console.h"

static uint8_t* cur = (uint8_t*) VRAM + (160*4);

void clearScreen() {
    for (uint64_t* v = (uint64_t*) VRAM; v < (uint64_t*) VRAM + 160/8*25; v++)
        *v = 0x0700070007000700;
    cur = (uint8_t*) VRAM;
}

static void advanceLine() {
    uint8_t* vram = (uint8_t*) VRAM;

    for (int i=0; i<24; i++)
        for (int j=0; j<160; j++)
            vram[i*160+j] = vram[(i+1)*160+j];

    for (uint64_t* v = (uint64_t*) VRAM + 160/8*24; v < (uint64_t*) VRAM + 160/8*25; v++)
         *v = 0x0700070007000700;
}

void printColor(char* s, uint8_t c) {
    while (*s != 0) {
        if (*s == '\n') {
            for (uint64_t n = 160 - ((uint64_t) cur - VRAM) % 160; n > 0; n -= 2) {
                *cur++ = 0;
                *cur++ = c;
            }
            *s++;
        } else {
            *cur++ = *s++;
            *cur++ = c;
        }

        if (cur >= (uint8_t*) VRAM + (160*25)) {
            advanceLine();
            cur = (uint8_t*) VRAM + (160*24);
        }
    }
}

void print(char* s) {
    printColor(s, 0x07);
}

// Have bottom line be a solid color background and have a clock and other status info?  (Or top line?)
//   Easy enough if this file supports it (with cur, clearScreen, and advanceLine (and printColor, if at bottom).
