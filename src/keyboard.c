#include <stdint.h>

#include "keyboard.h"

#include "list.h"

// https://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html

#define si(k) (struct input) {(k), alt_down, ctrl_down, shift_down}
#define shifty(c, n, s) case c: gotInput(shift_down ? si(s) : si(n)); break
#define capsy(c, n, s) case c: gotInput((shift_down && !caps_lock_on) || \
                                        (!shift_down && caps_lock_on) ? si(s) : si(n)); break

// Let's ignore caps lock for now, but keep track of shift
static uint8_t shift_down = 0;
static uint8_t ctrl_down = 0;
static uint8_t alt_down = 0;
static uint8_t caps_lock_on = 0; // We really mean 0 as unchanged from what it initially was...

static struct list* inputCallbackList = (struct list*) 0;

static inline void gotInput(struct input c) {
    forEachListItem(inputCallbackList, ({
        void __fn__ (void* item) {
            ((void (*)(struct input)) item)(c);
        }
        __fn__;
    }));
}

void keyScanned(uint8_t c) {
    uint8_t hob = c & 0x80;  // Break / release
    switch (c) {
    case 0x9d: // LCtrl (Or RCtrl if e0 was before this)
    case 0xaa: // LShift
    case 0xb6: // RShift
    case 0xb8: // LAlt (Or Ralt if e0 was before this)
        c -= 0x80;
        break;
    default:
        break;
    }

    switch (c) {
    case 0x1d:
        ctrl_down = hob ? 0 : 1;
        break;

    case 0x2a:
    case 0x36:
        shift_down = hob ? 0 : 1;
        break;

    case 0x38:
        alt_down = hob ? 0 : 1;
        break;

    case 0x3a:
        caps_lock_on = !caps_lock_on;
        break;

        shifty(0x02, '1', '!');
        shifty(0x03, '2', '@');
        shifty(0x04, '3', '#');
        shifty(0x05, '4', '$');
        shifty(0x06, '5', '%');
        shifty(0x07, '6', '^');
        shifty(0x08, '7', '&');
        shifty(0x09, '8', '*');
        shifty(0x0a, '9', '(');
        shifty(0x0b, '0', ')');
        //case 0x0b:
        //__asm__ __volatile__("int $0x23");
        //int n = 1/0;
        //break;
        shifty(0x0c, '-', '_');
        shifty(0x0d, '=', '+');

        capsy(0x10, 'q', 'Q');
        capsy(0x11, 'w', 'W');
        capsy(0x12, 'e', 'E');
        capsy(0x13, 'r', 'R');
        capsy(0x14, 't', 'T');
        capsy(0x15, 'y', 'Y');
        capsy(0x16, 'u', 'U');
        capsy(0x17, 'i', 'I');
        capsy(0x18, 'o', 'O');
        capsy(0x19, 'p', 'P');
        shifty(0x1a, '[', '{');
        shifty(0x1b, ']', '}');

        capsy(0x1c, '\n', '\n');

        capsy(0x1e, 'a', 'A');
        capsy(0x1f, 's', 'S');
        capsy(0x20, 'd', 'D');
        capsy(0x21, 'f', 'F');
        capsy(0x22, 'g', 'G');
        capsy(0x23, 'h', 'H');
        capsy(0x24, 'j', 'J');
        capsy(0x25, 'k', 'K');
        capsy(0x26, 'l', 'L');
        shifty(0x27, ';', ':');
        shifty(0x28, '\'', '"');

        capsy(0x2c, 'z', 'Z');
        capsy(0x2d, 'x', 'X');
        capsy(0x2e, 'c', 'C');
        capsy(0x2f, 'v', 'V');
        capsy(0x30, 'b', 'B');
        capsy(0x31, 'n', 'N');
        capsy(0x32, 'm', 'M');
        shifty(0x33, ',', '<');
        shifty(0x34, '.', '>');
        shifty(0x35, '/', '?');

        shifty(0x39, ' ', ' ');
    default:
        break;
    }
}

void registerKbdListener(void (*f)(struct input)) {
    if (!inputCallbackList)
        inputCallbackList = newList();

    addToList(inputCallbackList, f);
}

void unregisterKbdListener(void (*f)(struct input)) {
    if (!inputCallbackList)
        return;

    removeFromList(inputCallbackList, f);
}
