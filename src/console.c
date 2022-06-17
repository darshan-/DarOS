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

// Okay, new tentative approach to scrolling, eventual ctrl-l, top:
// We calculate top on the fly, based on cur and end.  These uniquely determine exactly how many lines to have above screen and where
//   the cursor is.  Then we apply v_clear, then v_scroll, to dermine how to actually map the viewport.

struct vterm {
    uint8_t** buf;
    uint64_t cap;

    uint64_t anchor; // Index after last non-editable character
    uint64_t cur;    // Index of cursor (edit point)
    uint64_t end;    // Index after last character

    uint64_t clear_top; // Top position to use due to ctrl-l / clear
    uint64_t v_scroll;  // Vertical offset from scrolling
};

static uint8_t at = 255;

static struct vterm terms[TERM_COUNT];

#define line_for(t, i) (terms[t].i / 160)
#define page_index_for(t, i) (terms[t].i / (LINES * 160))
#define page_for(t, i) (terms[t].buf[(i) / (LINES * 160)])
#define page_after(t, i) page_for(t, (i) + LINES * 160)
#define page_before(t, i) page_for(t, (i) - LINES * 160)
#define cur_page(t) page_for(t, terms[t].cur)
#define end_page(t) page_for(t, terms[t].end)
#define anchor_page(t) page_for(t, terms[t].anchor)
#define byte_at(t, i) page_for(t, i)[(i) % (LINES * 160)]
#define word_at(t, i) (((uint16_t*) page_for(t, i))[(i) % (LINES * 160) / 2])
#define qword_at(t, i) (((uint64_t*) page_for(t, i))[(i) % (LINES * 160) / 8])

static void addPage(uint8_t t, uint64_t i) {
    if (terms[t].buf[i])
        return;

    if (terms[t].cap < i + 2) {
        terms[t].cap *= 2;
        terms[t].buf = reallocz(terms[t].buf, terms[t].cap * sizeof(uint8_t*));
    }

    terms[t].buf[i] = malloc(LINES * 160);
    for (int j = 0; j < LINES * 20; j++)
        ((uint64_t*) terms[t].buf[i])[j] = 0x0700070007000700ull;
}

static inline void ensurePages(uint8_t t) {
    uint64_t i = page_index_for(t, end);

    if (!terms[t].buf[i])
        addPage(t, i);

    if (!terms[t].buf[i + 1])
        addPage(t, i + 1);
}

static inline uint64_t top(uint8_t t) {
    uint64_t l;

    if (line_for(t, end) - line_for(t, cur) >= LINES)
        l = line_for(t, cur);
    else if (line_for(t, end) - terms[t].clear_top < LINES)
        l = terms[t].clear_top;
    else
        l = line_for(t, end) - LINES + 1;

    return (l - terms[t].v_scroll) * 160;
}

static inline uint64_t curPositionInScreen(uint8_t t) {
    return terms[t].cur - top(t);
}

static inline void hideCursor() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

static inline void updateCursorPosition() {
    uint64_t c = curPositionInScreen(at);
    if (c >= LINES * 160) {
        hideCursor();
        return;
    }

    c /= 2;
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
        VRAM64[i] = qword_at(at, top(at) + i * 8);

    updateCursorPosition();
}

static void scrollDownBy(uint64_t n) {
    if (terms[at].v_scroll == 0)
        return;

    if (n < terms[at].v_scroll)
        terms[at].v_scroll -= n;
    else
        terms[at].v_scroll = 0;

    syncScreen();

    if (at != LOGS)
        showCursor();
}

static inline void scrollToBottom() {
    scrollDownBy((uint64_t) -1);
}

static void scrollUpBy(uint64_t n) {
    if (top(at) == 0)
        return;

    uint64_t max_scroll = top(at) / 160;
    if (n > max_scroll)
        n = max_scroll;

    terms[at].v_scroll += n;

    syncScreen();
}

static inline void scrollToTop() {
    scrollUpBy((uint64_t) -1);
}

static inline void clear() {
    scrollToBottom();

    if (line_for(at, anchor) == top(at) / 160)
        return;
    terms[at].clear_top = line_for(at, anchor);
    ensurePages(at);

    syncScreen();
}

static void prompt(uint8_t t) {
    scrollToBottom();
    terms[at].cur = terms[at].end;
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
        ensurePages(t);

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

static inline void backspace() {
    scrollToBottom();

    if (terms[at].cur == terms[at].anchor)
        return;

    for (uint64_t i = terms[at].cur; i < terms[at].end; i += 2)
        word_at(at, i - 2) = word_at(at, i);

    word_at(at, terms[at].end - 2) = 0x0700;

    terms[at].cur -= 2;
    terms[at].end -= 2;

    syncScreen();
}

static inline void delete() {
    scrollToBottom();

    if (terms[at].cur == terms[at].end)
        return;

    for (uint64_t i = terms[at].cur; i + 2 < terms[at].end; i += 2)
        word_at(at, i) = word_at(at, i + 2);

    word_at(at, terms[at].end - 2) = 0x0700;

    terms[at].end -= 2;

    syncScreen();
}

static inline void wordLeft() {
    scrollToBottom();

    if (terms[at].cur == terms[at].anchor)
        return;

    while (byte_at(at, terms[at].cur - 2) == ' ' && terms[at].cur != terms[at].anchor)
        terms[at].cur -= 2;
    while (byte_at(at, terms[at].cur - 2) != ' ' && terms[at].cur != terms[at].anchor)
        terms[at].cur -= 2;

    updateCursorPosition();
}

static inline void wordRight() {
    scrollToBottom();

    if (terms[at].cur == terms[at].end)
        return;

    while (byte_at(at, terms[at].cur) == ' ' && terms[at].cur != terms[at].end)
        terms[at].cur += 2;
    while (byte_at(at, terms[at].cur) != ' ' && terms[at].cur != terms[at].end)
        terms[at].cur += 2;

    updateCursorPosition();
}

static inline void cursorLeft() {
    scrollToBottom();

    if (terms[at].anchor == terms[at].cur)
        return;

    terms[at].cur -= 2;
    updateCursorPosition();
}

static inline void cursorRight() {
    scrollToBottom();

    if (terms[at].cur == terms[at].end)
        return;

    terms[at].cur += 2;
    updateCursorPosition();
}

static inline void cursorHome() {
    scrollToBottom();

    if (terms[at].anchor == terms[at].cur)
        return;

    terms[at].cur = terms[at].anchor;
    updateCursorPosition();
}

static inline void cursorEnd() {
    scrollToBottom();

    if (terms[at].cur == terms[at].end)
        return;

    terms[at].cur = terms[at].end;
    updateCursorPosition();
}

static inline void clearCurToAnchor() {
    scrollToBottom();

    if (terms[at].cur == terms[at].anchor)
        return;

    uint64_t cur_diff = terms[at].cur - terms[at].anchor;
    uint64_t i;
    for (i = 0; terms[at].cur + i < terms[at].end; i += 2)
        word_at(at, terms[at].anchor + i) = word_at(at, terms[at].cur + i);
    for (; terms[at].anchor + i < terms[at].end; i += 2)
        word_at(at, terms[at].anchor + i) = 0x0700;

    terms[at].cur -= cur_diff;
    terms[at].end -= cur_diff;

    syncScreen();
}

static inline void clearCurToEnd() {
    scrollToBottom();

    if (terms[at].cur == terms[at].end)
        return;

    for (uint64_t i = terms[at].cur; i < terms[at].end; i += 2)
        word_at(at, i) = 0x0700;

    terms[at].end = terms[at].cur;

    syncScreen();
}

static inline void printCharColor(uint8_t t, uint8_t c, uint8_t color) {
    uint64_t l = top(t);

    if (c == '\n') {
        for (uint64_t n = 160 - terms[t].cur % 160; n > 0; n -= 2)
            printcc(t, 0, color);
    } else {
        printcc(t, c, color);
    }

    if (top(t) != l)
        ensurePages(t);
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

// TODO:
//   (If I switch to page tables, ctrl-pgup to jump up 10 pages, ctrl-pgdn to jump down 10 pages?)
//
//   ctrl-j as enter?
//   alt-backspace to delete previous word?
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

    else if (at > 0) {
        if (isPrintable(i.key) && !i.alt && !i.ctrl) {
            scrollToBottom();
            printCharColor(at, i.key, 0x07);
            syncScreen();
        }

        else if (i.key == '\b' && !i.alt && !i.ctrl && !i.shift)
            backspace();

        else if (i.key == '\n' && !i.alt && !i.ctrl && !i.shift)
            prompt(at);

        else if (i.key == 'u' && !i.alt && i.ctrl && !i.shift)
            clearCurToAnchor();

        else if (i.key == 'k' && !i.alt && i.ctrl && !i.shift)
            clearCurToEnd();

        else if (i.key == KEY_DEL && !i.alt && !i.ctrl && !i.shift)
            delete();

        else if (i.key == KEY_LEFT && !i.alt && !i.ctrl && !i.shift)
            cursorLeft();

        else if ((i.key == KEY_LEFT || i.key == 'b') && i.alt && !i.ctrl && !i.shift)
            wordLeft();

        else if (i.key == KEY_RIGHT && !i.alt && !i.ctrl && !i.shift)
            cursorRight();

        else if ((i.key == KEY_RIGHT || i.key == 'f') && i.alt && !i.ctrl && !i.shift)
            wordRight();

        else if (i.key == KEY_HOME && !i.alt && !i.ctrl && !i.shift)
            cursorHome();

        else if (i.key == 'a' && !i.alt && i.ctrl && !i.shift)
            cursorHome();

        else if (i.key == KEY_END && !i.alt && !i.ctrl && !i.shift)
            cursorEnd();

        else if (i.key == 'e' && !i.alt && i.ctrl && !i.shift)
            cursorEnd();

        else if (i.key == 'h' && !i.alt && i.ctrl && !i.shift)
            backspace();

        else if (i.key == 'l' && !i.alt && i.ctrl && !i.shift)
            clear();
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
