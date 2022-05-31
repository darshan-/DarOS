#pragma once

#include <stdint.h>

// So letters, numbers, and symbols go in key as they symbol they are (e.g., 'd', 'D', '`', '~', ...).
// Modifiers set as appropriate (including indicating shift for letters and symbols that have it down,
//   I think...).
// So then it's just a matter of having standarized codes for the function keys, escape, backspace,
//   arrows, and anything else I might want to handle?  (Oh, enter can stay new line, I think.)
struct input {
    uint8_t key;
    uint8_t alt;
    uint8_t ctrl;
    uint8_t shift;
};

// struct input {
//     uint8_t key : 8;
//     uint8_t alt : 1;
//     uint8_t ctrl : 1;
//     uint8_t shift : 1;
// };

// So let's use ascii code for 32 (space) through 126 (~).
// Ascii's enter as 10 and tab as 9 would be good.
// And ascii has backspace as 8 and delete as 127.
// Oh, and escape as 27.
// So I think 11-26, inclusive, are available; putting function keys in there would be good.
//  (They don't have to be contiguous, but F1-F10 have contiguous scan codes, so it would be nice.)
// F11, F12, arrows, home, end, page up, page down, insert, maybe windows keys, maybe
//  print screen, maybe, sroll lock, maybe pause.

#define KEY_F1  11
#define KEY_F2  12
#define KEY_F3  13
#define KEY_F4  14
#define KEY_F5  15
#define KEY_F6  16
#define KEY_F7  17
#define KEY_F8  18
#define KEY_F9  19
#define KEY_F10 20
#define KEY_F11 21
#define KEY_F12 22

#define KEY_UP 23
#define KEY_DOWN 24
#define KEY_LEFT 25
#define KEY_RIGHT 26

#define KEY_ESCAPE 27
#define KEY_DELETE 127

#define KEY_TAB '\t'
#define KEY_BACKSPACE '\b'
#define KEY_ENTER '\n'

#define KEY_PG_UP 28
#define KEY_PG_DOWN 29
#define KEY_HOME 30
#define KEY_END 31

void keyScanned(uint8_t c);
void registerKbdListener(void (*)(struct input));
void unregisterKbdListener(void (*)(struct input));
void init_keyboard();
