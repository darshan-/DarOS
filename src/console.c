#include <stdarg.h>
#include <stdint.h>
#include "console.h"
#include "io.h"
#include "malloc.h"
#include "strings.h"

#define VRAM ((uint8_t*) 0xb8000)
#define LINES 24
#define LINE(l) (VRAM + 160 * (l))
#define LAST_LINE LINE(LINES - 1)
#define STATUS_LINE LINE(LINES)
#define VRAM_END LINE(LINES + 1)

static uint8_t* cur = VRAM;

static inline void updateCursorPosition() {
    uint64_t c = (uint64_t) (cur - VRAM) / 2;

    outb(0x3D4, 0x0F);
    outb(0x3D4+1, (uint8_t) (c & 0xff));
    outb(0x3D4, 0x0E);
    outb(0x3D4+1, (uint8_t) ((c >> 8) & 0xff));
}

void setStatusBar() {
    for (uint64_t* v = (uint64_t*) STATUS_LINE; v < (uint64_t*) VRAM_END; v++)
        *v = 0x3f003f003f003f00;
}

void clearScreen() {
    for (uint64_t* v = (uint64_t*) VRAM; v < (uint64_t*) STATUS_LINE; v++)
        *v = 0x0700070007000700;
    cur = VRAM;
    updateCursorPosition();
    setStatusBar();
}

static inline void advanceLine() {
    for (int i = 0; i < LINES - 1; i++)
        for (int j = 0; j < 160; j++)
            VRAM[i * 160 + j] = VRAM[(i + 1) * 160 + j];

    for (uint64_t* v = (uint64_t*) LAST_LINE; v < (uint64_t*) STATUS_LINE; v++)
        *v = 0x0700070007000700;
}

static inline void curAdvanced() {
    if (cur < STATUS_LINE)
        return;

    advanceLine();
    cur = LAST_LINE;
    // We don't want to waste time updating VGA cursor position for every character of a string, so don't
    //  call updateCursorPosition() here, but only at end of exported functions that move cursor.
}

static inline void printcc(uint8_t c, uint8_t cl) {
    *cur++ = c;
    *cur++ = cl;
}

static inline void printCharColor(uint8_t c, uint8_t color) {
    if (c == '\n')
        for (uint64_t n = 160 - ((uint64_t) (cur - VRAM)) % 160; n > 0; n -= 2)
            printcc(0, color);
    else
        printcc(c, color);

    curAdvanced();
}

static inline void printChar(uint8_t c) {
    printCharColor(c, 0x07);
}

void printColor(char* s, uint8_t c) {
    while (*s != 0)
        printCharColor(*s++, c);

    updateCursorPosition();
}

void print(char* s) {
    printColor(s, 0x07);
}

void printc(char c) {
    printCharColor(c, 0x07);
    updateCursorPosition();
}

void printf(char* fmt, ...) {
    VARIADIC_PRINT(print);
}

//void readline(void (*lineread)(char*))) { // how would we pass it back without dynamic memory allocation?
    // Set read start cursor location (which scrolls up with screen if input is over one line (with issue of
    //    what to do if it's a whole screenful undecided for now)).
    // Tell keyboard we're in input mode; when newline is entered (or ctrl-c, ctrl-d, etc.?), it calls a
    //   different callback that we pass to it? (Which calls this callback?)
//}

// Have bottom line be a solid color background and have a clock and other status info?  (Or top line?)
//   Easy enough if this file supports it (with cur, clearScreen, and advanceLine (and printColor, if at bottom).
