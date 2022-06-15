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

#define VRAM ((uint8_t*) 0xb8000)
#define VRAM64 ((uint64_t*) VRAM)
#define LINES 24
#define LINE(l) (VRAM + 160 * (l))
#define LAST_LINE LINE(LINES - 1)
#define STATUS_LINE LINE(LINES)
#define VRAM_END LINE(LINES + 1)

#define LOGS LOGS_TERM

#define TERM_COUNT 10 // One of which is logs

struct vterm {
    uint8_t** buf;
    uint64_t cap;

    uint64_t top;    // Index of top line in screen (how many characters are above screen)
    uint64_t off;    // Offset to support ctrl-l
    uint64_t cur;    // Index of cursor (edit point)
    uint64_t anchor; // Index after last non-editable character
    uint64_t end;    // Index after last character
};

static uint8_t at = 255;

static struct vterm terms[TERM_COUNT];

#define page_index_for(t, i) terms[t].i / (LINES * 160)
#define page_for(t, i) terms[t].buf[(i) / (LINES * 160)]
#define page_after(t, i) page_for(t, i + LINES * 160)
#define page_before(t, i) page_for(t, i - LINES * 160)
#define top_page(t) page_for(t, terms[t].top)
#define cur_page(t) page_for(t, terms[t].cur)
#define end_page(t) page_for(t, terms[t].end)
#define anchor_page(t) page_for(t, terms[t].anchor)
#define byte_at(t, i) page_for(t, i)[(i) % (LINES * 160)]
#define word_at(t, i) (((uint16_t*) page_for(t, i))[(i) % (LINES * 160) / 2])
#define qword_at(t, i) (((uint64_t*) page_for(t, i))[(i) % (LINES * 160) / 8])

static inline void addPage(uint8_t t) {
    if (end_page(t)) // May exist already (e.g., due to backspace decreasing end), so don't malloc unnecessarily or leak that page
        return;

    if (terms[t].cap < page_index_for(t, end) + 2) {
        terms[t].cap *= 2;
        terms[t].buf = reallocz(terms[t].buf, terms[t].cap * sizeof(uint8_t*));
    }

    end_page(t) = malloc(LINES * 160);
    for (int i = 0; i < LINES * 20; i++)
        ((uint64_t*) end_page(t))[i] = 0x0700070007000700ull;
}

static inline uint64_t curPositionInScreen(uint8_t t) {
    return terms[t].cur - terms[t].top;
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

void updateHeapUse() {
    char* s;
    uint64_t m = heapUsed();
    char* unit = "bytes";

    if (m >= 1024) {
        unit = "K";
        m /= 1024;
    }

    s = M_sprintf("Heap used: %u %s", m, unit);  // TODO: Round rather than floor and/or decimal point, etc.?

    const uint64_t max_len = 24;
    if (strlen(s) > max_len)
        s[max_len] = 0;

    if (strlen(s) < max_len) {
        char* f = M_sprintf("%%p %us", max_len);
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

    updateHeapUse(); // Clock will update very soon, but heap use won't for 2 seconds

    registerPeriodicCallback((struct periodic_callback) {60, 1, updateClock});
    registerPeriodicCallback((struct periodic_callback) {2, 1, updateHeapUse});
}

static void syncScreen() {
    for (uint64_t i = 0; i < LINES * 20; i++)
        VRAM64[i] = qword_at(at, terms[at].top + i * 8);

    updateCursorPosition();
}

static void prompt(uint8_t t) {
    if (terms[t].cur % 160 != 0)
        printTo(t, "\n");

    printColorTo(t, "\3 > ", 0x05);
    if (t == at)
        syncScreen();
}

static inline void ensureTerm(uint8_t t) {
    no_ints();
    if (!terms[t].buf) {
        terms[t].cap = 16;
        terms[t].buf = mallocz(terms[t].cap * sizeof(uint8_t*));
    }

    if (!terms[t].buf[0]) {
        addPage(t);

        if (t == LOGS_TERM) {
            printColorTo(t, "- Start of logs -\n", 0x0f);
        } else {
            char* s = M_sprintf(" (#%u)\n", t);
            printColorTo(t, "Ready!", 0x0d);
            printColorTo(t, s, 0x0b);
            free(s);
            prompt(t);
        }
    }
    ints_okay();
}

static inline void printcc(uint8_t t, uint8_t c, uint8_t cl) {
    if (terms[t].cur < terms[t].end)
        for (uint64_t i = terms[t].end; i > terms[t].cur; i -= 2)
            word_at(t, i) = word_at(t, i - 2);

    word_at(t, terms[t].cur) = (cl << 8) | c;

    terms[t].cur += 2;
    terms[t].end += 2;
}

/*
  Okay, I think the issue I noticed when backspacing across lines was because the code assumes in several places that cur is at the
    bottom of the screen once we've reached a screenful of output.  That won't be true once we have ctrl-l, but duh, it's also not true
    once you can backspace up a line.  Should that also use the offset mechanism I'd planned to use for ctrl-l, or is there are better
    approach to both?  At first glance, it seems still right.  I mean, given that I think backspacing up should have the same behavior --
    if your'e on the bottom line, type a little over a line of input, then backspace back up to the second-to-last line, then press down
    or page down, I don't think any scrolling down should happen.  I think the backspacing up a line should have created an offset, a new
    home base / vertical anchor.  And in fact, I just checked, after writing that, and bash in konsole works this way that I just
    described.  As I was entering spaces and about to try it, another option did occur to me, however: when backspace takes you up a line
    (or ctrl-u, which might do multiple lines at once, keep in mind) it would also feel natural, I think, to have the page scroll back
    down.  So essentially if the cursor is at the last position on the screen and you type a character, everything goes up one and the
    cursor is at the beginning of the last line, and if you press backspace, we could just undo that.  That's actually easy to do right
    now (with ctrl-u being a little harder but not hard, I think), and I kind like it, so I think I might try that for now.  Well, yeah,
    I will, because at least I'd have correct behavior until I have the offset set up and working, at which point I'll be able to pick
    which behavior I prefer.  I'm honestly not sure which I'll prefer, but in many ways I think I currently lean toward this, and I can
    do it now and have backspace working a correct way, and then I'll have a better sense which I want.  So, cool.
 */

// This *may* make sense to apply to non-active terminals, or even to handle a '\b' in a string for printing.
// But for now, I'm not sure if that's the case; let's just handle explicit backspace key pressed at console.
static inline void backspace() {
    if (terms[at].anchor == terms[at].cur)
        return;

    for (uint64_t i = terms[at].cur; i < terms[at].end; i += 2)
        word_at(at, i - 2) = word_at(at, i);

    word_at(at, terms[at].end - 2) = 0x0700;

    if (terms[at].cur % 160 == 0 && terms[at].top != 0)
        terms[at].top -= 160;

    terms[at].cur -= 2;
    terms[at].end -= 2;

    syncScreen();
}

static inline void cursorLeft() {
    if (terms[at].anchor == terms[at].cur)
        return;

    terms[at].cur -= 2;
    updateCursorPosition();
}

static inline void clearInput() {
    if (terms[at].anchor == terms[at].cur)
        return;

    //uint64_t page_index = terms[at].cur / (LINES * 160);
    uint64_t page_index = page_index_for(at, cur);
    if (terms[at].cur % (LINES * 160) == 0)
        page_index--;

    uint8_t* p;
    for (p = terms[at].buf[page_index]; p != anchor_page(at); p = terms[at].buf[page_index--]) {
        for (int i = 0; i < LINES * 20; i++)
            *((uint64_t*) p++) = 0x0700070007000700ull;

        uint64_t cur_diff = terms[at].cur % (LINES * 160);
        terms[at].top -= cur_diff / 160 * 160; // Looks funny, but seems the correct and obvious way to get whole number of lines
        terms[at].cur -= cur_diff;
        terms[at].end -= cur_diff;
    }

    if (terms[at].anchor != terms[at].cur) {
        uint64_t n = terms[at].cur - terms[at].anchor;
        uint16_t* q = (uint16_t*) p + (terms[at].anchor % (LINES * 160) / 2);

        for (uint64_t i = 0; i < n; i++)
            *q++ = 0x0700;

        uint64_t top_diff = (terms[at].cur / 160 - terms[at].anchor / 160) * 160;

        if (top_diff > terms[at].top)
            top_diff = terms[at].top;

        terms[at].top -= top_diff;
        terms[at].cur -= n;
        terms[at].end -= n;
    }

    syncScreen();
}

static inline void printCharColor(uint8_t t, uint8_t c, uint8_t color) {
    if (c == '\n') {
        uint64_t cpip = curPositionInScreen(t);
        for (uint64_t n = 160 - cpip % 160; n > 0; n -= 2)
            printcc(t, 0, color);
    } else {
        printcc(t, c, color);
    }

    if (terms[t].end % (LINES * 160) == 0)
        addPage(t);

    if (curPositionInScreen(t) == LINES * 160)
        terms[t].top += 160;
}

void printColorTo(uint8_t t, char* s, uint8_t c) {
    no_ints();

    ensureTerm(t);

    while (*s != 0)
        printCharColor(t, *s++, c);

    terms[t].anchor = terms[t].cur;

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
    terms[at].anchor = terms[at].cur;
    syncScreen();
    ints_okay();
}

void printf(char* fmt, ...) {
    VARIADIC_PRINT(print);
}

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
//
//   ctrl-l
//
//  All of these depend on being able to edit somewhere within input field before end of field, I think:
//   ctrl-a / home
//   ctrl-e / end
//   ctrl-k
//   del
//   left arrow, right arrow
//
//   How much would it be worth it to invalidate regions rather than the whole screen at once?
//     - whole screen
//     - line n
//     - lines m - n?
//     - positions m - n?

static inline int isPrintable(uint8_t c) {
    return c >= ' ' && c <= '~';
}

static void gotInput(struct input i) {
    no_ints();

    if (i.key >= '0' && i.key <= '9' && !i.alt && i.ctrl && !i.shift)
        showTerm(i.key - '0');

    else if (i.key == KEY_UP && !i.alt && !i.ctrl && (i.shift || at == 0))
        scrollUpBy(1);

    else if (i.key == KEY_DOWN && !i.alt && !i.ctrl && (i.shift || at == 0))
        scrollDownBy(1);

    else if (i.key == KEY_PG_DOWN && !i.alt && !i.ctrl && (i.shift || at == 0))
        scrollDownBy(LINES);

    else if (i.key == KEY_PG_UP && !i.alt && !i.ctrl && (i.shift || at == 0))
        scrollUpBy(LINES);

    else if (i.key == KEY_HOME && !i.alt && !i.ctrl && (i.shift || at == 0))
        scrollToTop();

    else if (i.key == KEY_END && !i.alt && !i.ctrl && (i.shift || at == 0))
        scrollToBottom();

    else if (i.key == KEY_RIGHT && !i.alt && i.ctrl && !i.shift)
        showTerm((at + 1) % 10);

    else if (i.key == KEY_LEFT && !i.alt && i.ctrl && !i.shift)
        showTerm((at + 9) % 10);

    else if (i.key == KEY_LEFT && !i.alt && !i.ctrl && !i.shift)
        cursorLeft();

    else if (at > 0) {
        if (isPrintable(i.key) && !i.alt && !i.ctrl) {
            scrollToBottom();
            printCharColor(at, i.key, 0x07);
            syncScreen();
        } else if (i.key == '\b' && !i.alt && !i.ctrl && !i.shift) {
            scrollToBottom();
            backspace();
        } else if (i.key == '\n' && !i.alt && !i.ctrl && !i.shift) {
            scrollToBottom();
            prompt(at);
        } else if (i.key == 'u' && !i.alt && i.ctrl && !i.shift) {
            scrollToBottom();
            clearInput();
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
