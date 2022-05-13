#include <stdint.h>
#include "hex.h"

#define nibbleToHex(n) n > '9' ? n + 'A' - '9' - 1 : n

void byteToHex(uint8_t b, char* s) {
    char bh = (b >> 4) + '0';
    char bl = (b & 0x0f) + '0';
    s[0] = nibbleToHex(bh);
    s[1] = nibbleToHex(bl);
}

void wordToHex(uint16_t w, char* s) {
    uint8_t b = (uint8_t) (w >> 8);
    byteToHex(b, s);
    b = (uint8_t) w;
    byteToHex(b, s+2);
}

void dwordToHex(uint32_t d, char* s) {
    uint16_t w = (uint16_t) (d >> 16);
    wordToHex(w, s);
    w = (uint16_t) d;
    wordToHex(w, s+4);
}

void qwordToHex(uint64_t q, char* s) {
    uint32_t d = (uint32_t) (q >> 32);
    dwordToHex(d, s);
    d = (uint32_t) q;
    dwordToHex(d, s+8);
}
