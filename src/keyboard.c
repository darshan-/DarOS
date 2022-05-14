#include <stdint.h>
#include "console.h"
#include "keyboard.h"

// https://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html

// Code, char if shift not down, char if shift down
#define map(c, n, s) case c: gotInput(shift_down ? s : n); break

// Let's ignore caps lock for now, but keep track of shift
static uint8_t shift_down = 0;

static void (*listenerCallback)(char);

// Call for characters that count as input (for now just print them...)
static inline void gotInput(char c) {
    //printc(c);
    listenerCallback(c);
}

void keyScanned(uint8_t c) {
    uint8_t hob = c & 0x80;
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
    case 0x2a:
    case 0x36:
        shift_down = hob ? 0 : 1;
        break;
        map(0x02, '1', '!');
        map(0x03, '2', '@');
        map(0x04, '3', '#');
        map(0x05, '4', '$');
        map(0x06, '5', '%');
        map(0x07, '6', '^');
        map(0x08, '7', '&');
        map(0x09, '8', '*');
        map(0x0a, '9', '(');
        map(0x0b, '0', ')');
        //case 0x0b:
        //__asm__ __volatile__("int $0x23");
        //int n = 1/0;
        //break;
        map(0x0c, '-', '_');
        map(0x0d, '=', '+');

        map(0x10, 'q', 'Q');
        map(0x11, 'w', 'W');
        map(0x12, 'e', 'E');
        map(0x13, 'r', 'R');
        map(0x14, 't', 'T');
        map(0x15, 'y', 'Y');
        map(0x16, 'u', 'U');
        map(0x17, 'i', 'I');
        map(0x18, 'o', 'O');
        map(0x19, 'p', 'P');
        map(0x1a, '[', '{');
        map(0x1b, ']', '}');

        map(0x1c, '\n', '\n');

        map(0x1e, 'a', 'A');
        map(0x1f, 's', 'S');
        map(0x20, 'd', 'D');
        map(0x21, 'f', 'F');
        map(0x22, 'g', 'G');
        map(0x23, 'h', 'H');
        map(0x24, 'j', 'J');
        map(0x25, 'k', 'K');
        map(0x26, 'l', 'L');
        map(0x27, ';', ':');
        map(0x28, '\'', '"');

        map(0x2c, 'z', 'Z');
        map(0x2d, 'x', 'X');
        map(0x2e, 'c', 'C');
        map(0x2f, 'v', 'V');
        map(0x30, 'b', 'B');
        map(0x31, 'n', 'N');
        map(0x32, 'm', 'M');
        map(0x33, ',', '<');
        map(0x34, '.', '>');
        map(0x35, '/', '?');

        map(0x39, ' ', ' ');
    default:
        break;
    }
}

// Have dynamic list of listeners, so more than one thing can listen, I think.
// For now have just one, or an array of 3 potential ones or something?
void registerKbdListener(void (*gotChar)(char)) {
    listenerCallback = gotChar;
}
