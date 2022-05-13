#include <stdint.h>
#include "console.h"
#include "keyboard.h"

// https://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html

// Let's ignore caps lock for now, but keep track of shift
static uint8_t shift_down = 0;

// Shift, ctrl, alt
void keyScanned(uint8_t c) {
    // uint8_t hob = c & 0x80;
    // uint8_t hob = 0;
    // if (c >= 0x80) {
    //     hob = 1;
    //     c -= 0x80;
    // }

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
    case 0x20:
        printc(shift_down ? 'D' : 'd');
        break;
    case 0x02:
        printc(shift_down ? '!' : '1');
        break;
    default:
        break;
    }
}
