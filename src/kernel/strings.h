#pragma once

#include <stdarg.h>
#include <stdint.h>

#include "malloc.h"

#define nibbleToHex(n) (n > '9' ? n + 'A' - '9' - 1 : n)

static inline void byteToHex(uint8_t b, char* s) {
    char bh = (b >> 4) + '0';
    char bl = (b & 0x0f) + '0';
    s[0] = nibbleToHex(bh);
    s[1] = nibbleToHex(bl);
}

static inline void wordToHex(uint16_t w, char* s) {
    uint8_t b = (uint8_t) (w >> 8);
    byteToHex(b, s);
    b = (uint8_t) w;
    byteToHex(b, s+2);
}

static inline void dwordToHex(uint32_t d, char* s) {
    uint16_t w = (uint16_t) (d >> 16);
    wordToHex(w, s);
    w = (uint16_t) d;
    wordToHex(w, s+4);
}

static inline void qwordToHex(uint64_t q, char* s) {
    uint32_t d = (uint32_t) (q >> 32);
    dwordToHex(d, s);
    d = (uint32_t) q;
    dwordToHex(d, s+8);
}

char* M_sprintf(char* fmt, ...);
char* sprintf(char* buf, uint64_t buf_len, char* fmt, ...);
char* M_vsprintf(char* s, uint64_t s_cap, char* fmt, va_list ap);
uint64_t strlen(char* s);
int strcmp(char* s, char* t);
char* M_sappend(char* s, char* t);
char* M_scopy(char* s);
// Decimal string to unsigned int
uint64_t dstoui(char* s);

#define VARIADIC_PRINT(p) \
    va_list ap; \
    va_start(ap, fmt); \
    char* s = M_vsprintf(0, 0, fmt, ap);         \
    va_end(ap); \
    p(s); \
    free(s)
