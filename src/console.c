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

static struct list* scrollUpBuf = (struct list*) 0;
static struct list* scrollDownBuf = (struct list*) 0;
static struct list *term_scrollUpBuf, *term_scrollDownBuf;

static inline void updateCursorPosition() {
    uint64_t c = (uint64_t) (cur - VRAM) / 2;

    outb(0x3D4, 0x0F);
    outb(0x3D4+1, (uint8_t) (c & 0xff));
    outb(0x3D4, 0x0E);
    outb(0x3D4+1, (uint8_t) ((c >> 8) & 0xff));
}

static inline void hideCursor() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

static inline void showCursor() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | 13);
 
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);
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
    registerPeriodicCallback((struct periodic_callback) {2, 1, updateMemUse});
}

void clearScreen() {
    no_ints();

    for (uint64_t* v = (uint64_t*) VRAM; v < (uint64_t*) STATUS_LINE; v++)
        *v = 0x0700070007000700;

    cur = VRAM;
    updateCursorPosition();

    ints_okay();
}

static inline void advanceLine() {
    if (!scrollUpBuf)
        scrollUpBuf = newList();

    uint8_t* line = malloc(160);
    for (int i = 0; i < 160; i++)
        line[i] = VRAM[i];
    pushListHead(scrollUpBuf, line);

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

    hideCursor();

    for (int i = 0; i < 160 * LINES; i++)
        term_buf[i] = VRAM[i];

    term_cur = cur;
    term_scrollUpBuf = scrollUpBuf;
    term_scrollDownBuf = scrollDownBuf;
    scrollUpBuf = (struct list*) 0;
    scrollDownBuf = (struct list*) 0;

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

    destroyList(scrollUpBuf);
    destroyList(scrollDownBuf);

    scrollUpBuf = term_scrollUpBuf;
    scrollDownBuf = term_scrollDownBuf;

    con = CON_TERM;

    if (listLen(scrollDownBuf) == 0)
        showCursor();
}

/*
  I'm pretty confident that logs as terminal 2 is just a temporary hack, and the issues I have with it (starting
  at the beginning every time seems wrong, but so is anything else; logs can be added while we're away, and keeping
  track of which ones we've printed and which we haven't is hairy), I just think isn't worth it.  I think multiple
  consoles/terminals sounds great, and a command to page through logs sounds great.  With multiple terminals, things
  will only be written to them while we're there.  (Or, huh, is that a bad limitation too?  Do I really want an extra
  level if indirection, so an app running on terminal 3 can write to terminal 3 while we're at terminal 1, and when
  we go back to terminal 3, we see what was output while we were away?  Ultimately, yeah, I guess that's what we'd
  want.  A buffer of some sort?  Instead of printing to the screen, it'd be printing to whatever terminal it's
  running on, and that's copied to the terminal right away if we're there, otherwise it's copied in once we switch
  back to it.  I guess writing to the terminal automatically scrolls us to bottom?  I think linux works that way; it
  feels pretty intuitive.  So we can move away and come back and still be scrolled wherever, as long as nothing was
  output, but if anything got written to the screen, we'll lose that position and be at the bottom.  I think as far
  as incremental improvements, it's still okay and in the spirit of things to not ever-engineer this, and just keep
  having fun and doing what makes sense and is interesting for now.)
 */

/*
  I think I want to do the ugly, easy thing for now, and just special case the main console and log console, and
  not worry about process output stuff yet.  So a scrollback and scrollahead buffer for whichever console I'm on,
  and store/restore those when moving from/to main console, but just recreate log one on demand.  It's the wrong
  model, but I think the best/easiest/funnest thing short term.
 */

static inline void scrollUp() {
    if (!scrollUpBuf)
        return;

    uint8_t* top = (uint8_t*) popListHead(scrollUpBuf);
    if (!top)
        return;

    hideCursor();

    if (!scrollDownBuf)
        scrollDownBuf = newList();

    uint8_t* bot = malloc(160);
    for (int i = 0; i < 160; i++)
        bot[i] = VRAM[160 * (LINES - 1) + i];
    pushListHead(scrollDownBuf, bot);

    for (int i = LINES - 1; i > 0; i--)
        for (int j = 0; j < 160; j++)
            VRAM[i * 160 + j] = VRAM[(i - 1) * 160 + j];

    for (int i = 0; i < 160; i++)
        VRAM[i] = top[i];

    free(top);
}

static inline void scrollDown() {
    if (!scrollDownBuf)
        return;

    uint8_t* bot = (uint8_t*) popListHead(scrollDownBuf);
    if (!bot)
        return;

    advanceLine();

    for (int i = 0; i < 160; i++)
        VRAM[160 * (LINES - 1) + i] = bot[i];

    free(bot);

    if (con == CON_TERM && listLen(scrollDownBuf) == 0)
        showCursor();
}


// Strangely, the line-at-a-time scrolling seems to work perfectly fine on bochs, exactly as I'd first imagined it,
//   and I can't reproduce the bug where it starts to take two attempts to switch consoles.  Still happens reliably
//   after a bit of stress with qemu, but I can't make it happen with bochs.  Next, I'll try the Presario.
// Yeah, works perfectly on Presario.  I knew it was an inefficient first draft, but I figured it'd be fine to scroll
//  a line at time if there weren't like a 1000 lines.  And with about a 100 (4 screenfulls) on the Presario, it's
//  exactly as I imagned.  You scroll to the top, you type something, and you're virtually instantly at the bottom.
// And I can't reproduce the ignore-console-switches bug there either.  Although I did get a weird default PIC
//  interrupt (and I don't currently macro-create them to see which one).  So inefficient but essetially working
//  perfectly everywhere but qemu, other than potential issue with the interrupt.  Except a PIC interrupt wouldn't
//  ever be a processor-driven issue, right?  It's like, for real hardware letting you know about something physical
//  happening, not the CPU letting you know you're doing something wrong?  Plus, I have everything masked except
//  keyboard and PIT, right?  So how'd I get it?  (VirtualBox can also reproduce the ignore switch issue...)

static inline void scrollToBottom() {
    if (!scrollDownBuf)
        return;

    // Decide how many lines to scroll down (max(length of scrollDownBuf, LINES))
    //    Hmm, well, not exactly.
    // Ah, however many lines are in scrollDownBuf are how many back we want to copy from the screen, and if
    //   greater than LINES, then not copying from the screen, yes.
    // But we want to copy 24 lines into the screen (from some combination of screen and scrollDownBuf), and
    //  copy however many lines are in scrollDownBuf into scrollUpBuf (from some combination of screen and
    //  scrollUpBuf).
    // I'm curious how helpful a recycle arena thing would be... But for now, let's now worry about that.

    uint32_t l = listLen(scrollDownBuf);

    if (l == 0)
        return;

    //for (int i = 0; i < l && i < l LINES - 1; i++) {
    //for (int i = 0; i < LINES - l; i++) {

    uint32_t i;
    for (i = 0; i < l && i < LINES; i++) {
        uint8_t* line = malloc(160);
        for (int j = 0; j < 160; j++)
            line[j] = VRAM[i * 160 + j];
        pushListHead(scrollUpBuf, line);
    }
    for (; i < l; i++)
        pushListHead(scrollUpBuf, popListHead(scrollDownBuf));

    for (i = 0; i < (int64_t) LINES - l; i++) {
        for (int j = 0; j < 160; j++)
            VRAM[i* 160 + j] = VRAM[(i + l) * 160 + j];
    }

    for (; i < LINES; i++) {
        uint8_t* line = (uint8_t*) popListHead(scrollDownBuf);
        for (int j = 0; j < 160; j++)
            VRAM[(i * 160) + j] = line[j];
        free(line);
    }

    showCursor();
}

static void gotInput(struct input i) {
    if (i.key == '1' && !i.alt && i.ctrl)
        showTerminal();

    else if (i.key == KEY_UP && !i.alt && !i.ctrl)
        scrollUp();

    else if (i.key == KEY_DOWN && !i.alt && !i.ctrl)
        scrollDown();

    else if (con == CON_TERM) {
        if (!i.alt && !i.ctrl) {
            scrollToBottom();
            printc(i.key);
        }

        else if (i.key == '2' && !i.alt && i.ctrl)
            showLogs();

        else if ((i.key == 'l' || i.key == 'L') && !i.alt && i.ctrl) // How shall this interact with scrolling?
            clearScreen(); // TODO: Nope, not anymore, since we have scrollback
    }
}

void startTty() {
    log("starting tty\n");
    init_keyboard();
    registerKbdListener(&gotInput);
    clearScreen();
    setStatusBar();
    printColor("Ready!\n", 0x0d);
    showCursor();
};
