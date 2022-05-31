#include <stdarg.h>
#include <stdint.h>

#include "console.h"

#include "interrupt.h"
#include "io.h"
#include "keyboard.h"
#include "malloc.h"
#include "periodic_callback.h"
#include "rtc.h"
#include "strings.h"

/*
  Hmm, it would be pretty easy to have a scrollback buffer...
  When advancing a line, before copying the second line into the first, copy the first
    to the end of the scrollback buffer.  Bind up and down arrows (or however you'd like
    the UI to be) to a scroll function... well, advanceLine *would* be the scroll down
    function, essentially.
  Hmm, either that, or actually waste a little memory if it makes it easier, and have
    *full* terminal buffer, not just scrollback buffer, and scrolling copies relevant
    part in.  I like that, and it would make it easier to have multiple terminals --
    which was the thought that spurred this thought -- I had the thought that I might
    like a log terminal, to use like I use the serial console, but with color.  I'm not
    sure, but I like the idea of flagging in red certain things, for example.  So multiple
    terminals, each with a complete buffer in memory, and scrolling or switching buffers
    is just a matter of what I copy into vram.
 */

#define VRAM ((uint8_t*) 0xb8000)
#define LINES 24
#define LINE(l) (VRAM + 160 * (l))
#define LAST_LINE LINE(LINES - 1)
#define STATUS_LINE LINE(LINES)
#define VRAM_END LINE(LINES + 1)

#define CON_TERM 1
#define CON_LOGS 2

static uint8_t* cur = VRAM;
static uint8_t con = CON_TERM;
static uint8_t* term_cur;

static inline void updateCursorPosition() {
    uint64_t c = (uint64_t) (cur - VRAM) / 2;

    outb(0x3D4, 0x0F);
    outb(0x3D4+1, (uint8_t) (c & 0xff));
    outb(0x3D4, 0x0E);
    outb(0x3D4+1, (uint8_t) ((c >> 8) & 0xff));
}

static void writeStatusBar(char* s, uint8_t loc) {
    if (loc >= 80) return;

    no_ints();
    for (uint8_t l = loc; *s && l < 160; s++, l++)
        STATUS_LINE[l*2] = *s;
    ints_okay();
}

#define MAX_MEMLEN 24
void updateMemUse() {
    char* s;
    uint64_t m = memUsed();
    char* unit = "bytes";

    if (m >= 1024) {
        unit = "K";
        m /= 1024;
    }

    s = M_sprintf("Heap used: %u %s", m, unit);  // TODO: Round rather than floor and/or decimal point, etc.?

    if (strlen(s) > MAX_MEMLEN)
        s[MAX_MEMLEN] = 0;

    // Zero-pad so things don't look bad if an interrupt happens after clearing but before writing.
    if (strlen(s) < MAX_MEMLEN) {
        char* f = M_sprintf("%%p %us", MAX_MEMLEN);
        char* t = M_sprintf(f, s);
        free(f);
        free(s);
        s = t;
    }

    writeStatusBar(s, 80 - strlen(s));
    free(s);
}

void updateClock() {
    struct rtc_time t;
    get_rtc_time(&t);

    char* ampm = t.hours >= 12 ? "PM" : "AM";
    uint8_t hours = t.hours % 12;
    if (hours == 0) hours = 12;
    char *s = M_sprintf("%p 2u:%p02u:%p02u.%p03u %s", hours, t.minutes, t.seconds, t.ms, ampm);

    writeStatusBar(s, 0);

    free(s);
}

static void setStatusBar() {
    for (uint64_t* v = (uint64_t*) STATUS_LINE; v < (uint64_t*) VRAM_END; v++)
        *v = 0x5f005f005f005f00;

    writeStatusBar("PurpOS", 37);

    updateMemUse(); // Clock will update very soon, but mem won't for 2 seconds

    registerPeriodicCallback((struct periodic_callback) {60, 1, updateClock});
    registerPeriodicCallback((struct periodic_callback) {1, 2, updateMemUse});
}

void clearScreen() {
    no_ints();

    for (uint64_t* v = (uint64_t*) VRAM; v < (uint64_t*) STATUS_LINE; v++)
        *v = 0x0700070007000700;

    cur = VRAM;
    updateCursorPosition();

    ints_okay();
}

// Two stacks of pointers to uint8_t[160]?  LIFO for both scroll back and scroll forward...
// So linked list, but push to head, not tail, right?
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
    no_ints();

    while (*s != 0)
        printCharColor(*s++, c);

    updateCursorPosition();

    ints_okay();
}

void print(char* s) {
    printColor(s, 0x07);
}

void printc(char c) {
    no_ints();

    printCharColor(c, 0x07);
    updateCursorPosition();

    ints_okay();
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


// For now maybe lets have no scrolling, just have a static buffer that holds a screenful of data (including color).

uint8_t term_buf[160 * LINES];

static inline void showLogs() {
    if (con == CON_LOGS)
        return;

    for (int i = 0; i < 160 * LINES; i++)
        term_buf[i] = VRAM[i];

    term_cur = cur;

    con = CON_LOGS;
    clearScreen();

    forEachLog(({
        void __fn__ (char* s) {
            print(s);
        }
        __fn__;
    }));
}

static inline void showTerminal() {
    if (con == CON_TERM)
        return;

    for (int i = 0; i < 160 * LINES; i++)
        VRAM[i] = term_buf[i];

    cur = term_cur;
    updateCursorPosition();

    con = CON_TERM;
}

static void gotInput(struct input i) {
    if (i.key == '1' && !i.alt && i.ctrl)
        showTerminal();

    // Things that apply to both go here.

    if (con == CON_TERM) {
        if (!i.alt && !i.ctrl)
            printc(i.key);

        if (i.key == '2' && !i.alt && i.ctrl)
            showLogs();

        if ((i.key == 'l' || i.key == 'L') && !i.alt && i.ctrl)
            clearScreen();
    }
}

void startTty() {
    log("starting tty\n");
    init_keyboard();
    registerKbdListener(&gotInput);
    clearScreen();
    setStatusBar();
    printColor("Ready!\n", 0x0d);
};
