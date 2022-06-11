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

// I'd planned on 25 lines per buffer page, as it fits in 4096-byte malloc page, but clearing/copying with 64-bit words means we need
//   an even number of lines, so 24 is easier.  And it turn out to be otherwise convenient to have visible lines the same as buffer
//   pages, like checking if we're at the bottom by there being no next page.  Obviously things will get more complicated again if I
//   eve want to remove the bottom status bar or add a top bar or anything.  But for now, lets go with just LINES for both things...

#define VRAM ((uint8_t*) 0xb8000)
#define LINES 24
#define LINE(l) (VRAM + 160 * (l))
#define LAST_LINE LINE(LINES - 1)
#define STATUS_LINE LINE(LINES)
#define VRAM_END LINE(LINES + 1)

#define LOGS LOGS_TERM

#define TERM_COUNT 10 // One of which is logs

/*
  Okay, I think I like the idea of each vterm being backed by one linked list of pages of memory, not a combination of the VRAM
    itself (or the array backing that up) and two linked lists of lines.  And we never write directly to screen, only to the backing
    store, which we then sync to the display.

  I probably want pages larger than 4K (about one screenful), so I'll want to improve malloc soon.

  But for now we can, I think, clean up, simplify, and increase performance by using either a linked list of 4000-byte pages (25x160)
    or an array of addresses of such pages.

  Since for this, we don't have anything curses like, whether in terminal or logs, we only write to the bottom.  Instead of worrying
    abou scrolling while writing, we only worry about finishing a page and starting a new one.  Then we scroll by adjusting the window
    if necessary.

  My hypothesis is that video memory is slower than normal RAM, so scrolling by reading and writing from/to vram is slower then what we
    hopefully will see when we never read from video RAM, just do one screenful of writes.  (I guess for some writes we do just want to
    add something to blank space on the screen, so that'll have to be worked out, but all in all, I think this will be cleaner code and
    more effiecient.)

  I do rather like the idea of an array of heads -- a page table.  So we can jump wherever we want easily.  Although I supposed a doubly
    linked list is good enough too.  We either want to jump to the beginning (head) end (tail), next page (next) or preview page (prev).
    We otherwise don't jump to any arbitrary location.  As long as I store head and tail of list, and prev and next in each node, it
    seems it would work fine.  But for some reason the array seems nicer or easier somehow...  Maybe I just prefer subscripts and
    arithmetic rather than using arrow operators on struct pointers?

  I'd still be limited right now by max 4096 malloc, unless I had mulitple arrays or used a static fixed one.  Most dynamic option
    currently then is that doubly linked list then, I guess.
 */

// Oh, we *will* want to jump to an arbitrary page, if we go away and come back, unless we keep a pointer to a currently shown page,
//   which I guess was the idea, which is why I had in mind we'll never *jump* to an arbitrary page.  So yeah, point is, keep another
//   pointer in here, and scroll position.
//
// Okay, so `line' will be the line at the top of the screen, so that combined with `cur' will uniquely identify what page
//   `page' is.

struct vterm {
    uint8_t* buf[2048];

    uint64_t top;    // Index of top line in screen (how many characters are above screen)
    uint64_t off;    // Offset to support ctrl-l
    uint64_t cur;    // Index of cursor (edit point)
    uint64_t anchor; // Index after last non-editable character
    uint64_t end;    // Index after last character
};

static uint8_t at = 255;

static struct vterm terms[TERM_COUNT];

#define page_top(t) terms[t].buf[terms[t].top / (LINES * 160)]
#define page_cur(t) terms[t].buf[terms[t].cur / (LINES * 160)]

static inline void addPage(uint8_t term) {
    uint64_t* p = malloc(LINES * 160);
    page_cur(term) = (uint8_t*) p;

    for (int i = 0; i < LINES * 20; i++)
        *p++ = 0x0700070007000700ull;
}

static inline uint64_t curPositionInScreen(uint8_t term) {
    return terms[term].cur - terms[term].top;
}

static inline void updateCursorPosition() {
    uint64_t c = curPositionInScreen(at) / 2;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (c & 0xff));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((c >> 8) & 0xff));
}

static inline void showCursor() {
    if (curPositionInScreen(at) >= LINES * 160)
        return;

    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | 13);
 
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);
}

static inline void hideCursor() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
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
        *v = 0x5f005f005f005f00ull;

    writeStatusBar("PurpOS", 37);

    updateMemUse(); // Clock will update very soon, but mem won't for 2 seconds

    registerPeriodicCallback((struct periodic_callback) {60, 1, updateClock});
    registerPeriodicCallback((struct periodic_callback) {2, 1, updateMemUse});
}

// static void clearScreen() {
//     no_ints();

//     for (uint64_t* v = (uint64_t*) VRAM; v < (uint64_t*) STATUS_LINE; v++)
//         *v = 0x0700070007000700ull;

//     ints_okay();
// }

// We want to only update screen when done with current update.
// For a keypress we're only adding a character. But something might right multiple characters, multiple lines, or even multiple pages
//   at once, we we don't want to do anything with the screen until the end of that call.
// So I guess have the exported function call sync at the very end?  It complicates exported functions using other exported functions,
//   but I can probably work something reasonable out.

static void syncScreen() {
    const uint64_t pos_in_pg = terms[at].top % (LINES * 160) / 8;
    uint64_t* v = (uint64_t*) VRAM;
    uint64_t* p = (uint64_t*) page_top(at) + pos_in_pg;

    uint64_t i;
    for (i = 0; i < (LINES * 20) - pos_in_pg; i++)
        *v++ = *p++;

    p = (uint64_t*) page_cur(at);

    for (; i < LINES * 20; i++)
        *v++ = *p++;

    updateCursorPosition();
}

static inline void printcc(uint8_t term, uint8_t c, uint8_t cl) {
    *((uint16_t*) page_cur(term) + terms[term].cur % (LINES * 160) / 2) = (cl << 8) | c;
    terms[term].cur += 2;
}

static inline void printCharColor(uint8_t term, uint8_t c, uint8_t color) {
    if (c == '\n') {
        uint64_t cpip = curPositionInScreen(term);
        for (uint64_t n = 160 - cpip % 160; n > 0; n -= 2)
            printcc(term, 0, color);
    } else {
        printcc(term, c, color);
    }

    if (terms[term].cur % (LINES * 160) == 0)
        addPage(term);

    if (curPositionInScreen(term) == LINES * 160)
        terms[term].top += 160;
}

static inline void ensureTerm(uint8_t t) {
    no_ints();
    if (!terms[t].buf[0]) {
        addPage(t);

        if (t == LOGS_TERM) {
            printColorTo(t, "- Start of logs -\n", 0x0f);
        } else {
            char* s = M_sprintf(" (#%u)\n", t);
            printColorTo(t, "Ready!", 0x0d);
            printColorTo(t, s, 0x0b);
            free(s);
        }
    }
    ints_okay();
}

void printColorTo(uint8_t t, char* s, uint8_t c) {
    no_ints();

    ensureTerm(t);

    while (*s != 0)
        printCharColor(t, *s++, c);

    ints_okay();
}

void printColor(char* s, uint8_t c) {
    no_ints();
    printColorTo(at, s, c);
    syncScreen();
    ints_okay();
}

void printTo(uint8_t t, char* s) {
    printColorTo(t, s, 0x07);
}

void print(char* s) {
    printColor(s, 0x07);
}

void printc(char c) {
    no_ints();
    printCharColor(at, c, 0x07);
    syncScreen();
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

static void scrollDownBy(uint64_t n);

static inline void scrollToBottom() {
    scrollDownBy((uint64_t) -1);
}

static void scrollUpBy(uint64_t n);

static inline void scrollToTop() {
    scrollUpBy((uint64_t) -1);
}

static void showTerm(uint8_t t) {
    if (t == at)
        return;

    at = t;

    if (t == LOGS)
        hideCursor();

    ensureTerm(t);

    syncScreen();

    if (t != LOGS)
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

static void scrollUpBy(uint64_t n) {
    if (terms[at].top == 0)
        return;

    hideCursor();

    if (n > terms[at].top / 160)
        n = terms[at].top / 160;

    terms[at].top -= n * 160;

    syncScreen();
}

static void scrollDownBy(uint64_t n) {
    if (curPositionInScreen(at) < LINES * 160)
        return;

    if (n > curPositionInScreen(at) / 160 - LINES + 1)
        n = curPositionInScreen(at) / 160 - LINES + 1;

    terms[at].top += n * 160;

    syncScreen();

    if (at != LOGS)
        showCursor();
}

// TODO:
//   (If I switch to page tables, ctrl-pgup to jump up 10 pages, ctrl-pgdn to jump down 10 pages?)
//   ctrl-l to work how I want
//
//   How much would it be worth it to invalidate regions rather than the whole screen at once?
//     - whole screen
//     - line n
//     - lines m - n?
//     - positions m - n?

static inline int isPrintable(uint8_t c) {
    return ((c >= ' ' && c <= '~') || c == '\n');
}

static void gotInput(struct input i) {
    no_ints();

    if (i.key >= '0' && i.key <= '9' && !i.alt && i.ctrl)
        showTerm(i.key - '0');

    else if (i.key == KEY_UP && !i.alt && !i.ctrl)
        scrollUpBy(1);

    else if (i.key == KEY_DOWN && !i.alt && !i.ctrl)
        scrollDownBy(1);

    else if (i.key == KEY_PG_DOWN && !i.alt && !i.ctrl)
        scrollDownBy(LINES);

    else if (i.key == KEY_PG_UP && !i.alt && !i.ctrl)
        scrollUpBy(LINES);

    else if (i.key == KEY_HOME && !i.alt && i.ctrl)
        scrollToTop();

    else if (i.key == KEY_END && !i.alt && i.ctrl)
        scrollToBottom();

    else if (i.key == KEY_RIGHT && !i.alt && i.ctrl)
        showTerm((at + 1) % 10);

    else if (i.key == KEY_LEFT && !i.alt && i.ctrl)
        showTerm((at + 9) % 10);

    else if (at > 0) {
        if (isPrintable(i.key) && !i.alt && !i.ctrl) {
            scrollToBottom();
            printc(i.key);
            syncScreen();
            if (i.key == 'd')
                log("d was typed\n");
            if (i.key == 'f')
                log("f was typed\n");
        }

        // TODO: Nope, not clearScreen() anymore, since we have scrollback.
        //   We'll want to scroll, kind of, but differently from advanceLine() and scrollDown(), by our line
        //    number minus 1.  If we're at line 10, then we want to basically scroll down by 9.  But a bit
        //    differently, I think; and we'll want to double-check assumptions other functions are making, in
        //    case anyone's assuming we can't have a scrollback buffer while not being at the bottom.
        // else if ((i.key == 'l' || i.key == 'L') && !i.alt && i.ctrl) // How shall this interact with scrolling?
        //     clearScreen();
    }
    ints_okay();
}

void startTty() {
    log("Starting tty\n");

    no_ints();
    init_keyboard();
    registerKbdListener(&gotInput);
    setStatusBar();
    showTerm(1);
    ints_okay();
};
