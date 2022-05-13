#include <stdint.h>
#include "hexoutput.h"

static inline void nibbleToHex(uint8_t* n) {
    //void nibbleToHex(uint8_t* n) {
    if (*n > '9')
        *n += 'A' - '9' - 1;
}

void hexoutByte(uint8_t b, void (*printCharf)(uint8_t)) {
        uint8_t bh = (b >> 4) + '0';
        uint8_t bl = (b & 0x0f) + '0';
        nibbleToHex(&bh);
        nibbleToHex(&bl);
        printCharf(bh);
        printCharf(bl);
}

void hexoutWord(uint16_t w, void (*printCharf)(uint8_t)) {
    uint8_t b = (uint8_t) (w >> 8);
    hexoutByte(b, printCharf);
    b = (uint8_t) w;
    hexoutByte(b, printCharf);
}

void hexoutDword(uint32_t d, void (*printCharf)(uint8_t)) {
    uint16_t w = (uint16_t) (d >> 16);
    hexoutWord(w, printCharf);
    w = (uint16_t) d;
    hexoutWord(w, printCharf);
}

void hexoutQword(uint64_t q, void (*printCharf)(uint8_t)) {
    uint32_t d = (uint32_t) (q >> 32);
    hexoutDword(d, printCharf);
    d = (uint32_t) q;
    hexoutDword(d, printCharf);
}
