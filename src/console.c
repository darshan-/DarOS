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

static inline void nibbleToHex(uint8_t* n) {
    if (*n > '9')
        *n += 'A' - '9' - 1;
}

void printByte(uint8_t b) {
        uint8_t bh = (b >> 4) + '0';
        uint8_t bl = (b & 0x0f) + '0';
        nibbleToHex(&bh);
        nibbleToHex(&bl);
        printChar(bh);
        printChar(bl);
}

void printWord(uint16_t w) {
    uint8_t b = (uint8_t) (w >> 8);
    printByte(b);
    b = (uint8_t) w;
    printByte(b);
}

void printDword(uint32_t d) {
    uint16_t w = (uint16_t) (d >> 16);
    printWord(w);
    w = (uint16_t) d;
    printWord(w);
}

void printQword(uint64_t q) {
    uint32_t d = (uint32_t) (q >> 32);
    printDword(d);
    d = (uint32_t) q;
    printDword(d);
}

// Have bottom line be a solid color background and have a clock and other status info?  (Or top line?)
//   Easy enough if this file supports it (with cur, clearScreen, and advanceLine (and printColor, if at bottom).
