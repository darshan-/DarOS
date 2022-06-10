#include <stdint.h>

#include "keyboard.h"

#include "interrupt.h"
#include "io.h"
#include "list.h"

// https://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html

#define si(k) (struct input) {(k), alt_down, ctrl_down, shift_down}
#define map(c, i) case c: gotInput(i); break
#define shifty(c, k, s) map(c, shift_down ? si(s) : si(k))
#define capsy(c, k, s) map(c, (shift_down && !caps_lock_on) || (!shift_down && caps_lock_on) ? si(s) : si(k))

static uint8_t shift_down = 0;
static uint8_t ctrl_down = 0;
static uint8_t alt_down = 0;
static uint8_t caps_lock_on = 0; // We really mean 0 as unchanged from what it initially was...
static uint8_t last_e0 = 0;

static struct list* inputCallbackList = (struct list*) 0;

static inline void gotInput(struct input c) {
    forEachListItem(inputCallbackList, ({
        void __fn__ (void* item) {
            ((void (*)(struct input)) item)(c);
        }
        __fn__;
    }));
}

static inline void kbd_out(uint8_t val) {
    while(inb(0x64) & 0b10) // TODO: Is this safe, or should I have a counter and crap out after a bit?
        ;
    outb(0x60, val);
}

static inline uint8_t kbd_read_resp() {
    while(!(inb(0x64) & 0b1)) // TODO: Is this safe, or should I have a counter and crap out after a bit?
        ;
    return inb(0x60);
}

#define KBD_ACK 0xfa
#define KBD_RESEND 0xfe

static inline void kbd_cmd(uint8_t cmd, uint8_t arg) {
    no_ints();

    for (int i = 0; i < 3; i++) {
        kbd_out(cmd);
        kbd_out(arg);

        uint8_t resp = kbd_read_resp();

        if (resp == KBD_ACK)
            break;

        if (resp == KBD_RESEND)
            continue;

        logf("Keyboard controller sent unexpected response: 0x%h\n", resp);
    }

    ints_okay();
}

void init_keyboard() {
    kbd_cmd(0xf3, 0b0100000); // Set Typematic to 0.5 sec delay, 30.0 per second
}

void keyScanned(uint8_t c) {
    uint8_t hob = c & 0x80;  // Break / release

    // Some things (e.g. 0x1c: enter) are handled the same whether e0 was sent, so this is up here and we return early if we're done
    if (last_e0) {
        switch (c) {
            map(0x35, si('/'));

            map(0x48, si(KEY_UP));
            map(0x50, si(KEY_DOWN));

            map(0x49, si(KEY_PG_UP));
            map(0x51, si(KEY_PG_DOWN));
        default:
            break;
        }

        switch (c) {
        case 0x35:
        case 0x48:
        case 0x49:
        case 0x50:
        case 0x51:
            last_e0 = 0;
            return;
        }
    }

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
        kbd_cmd(0xed, caps_lock_on << 2);
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

        map(0x1c, si('\n'));

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
        shifty(0x29, '`', '~');

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

        map(0x37, si('*'));
        map(0x47, si('7'));
        map(0x48, si('8'));
        map(0x49, si('9'));
        map(0x4a, si('-'));
        map(0x4b, si('4'));
        map(0x4c, si('5'));
        map(0x4d, si('6'));
        map(0x4e, si('+'));
        map(0x4f, si('1'));
        map(0x50, si('2'));
        map(0x51, si('3'));
        map(0x52, si('0'));
        map(0x53, si('.'));
    default:
        break;
    }

    if (c == 0xe0)
        last_e0 = 1;
    else
        last_e0 = 0;
}

void registerKbdListener(void (*f)(struct input)) {
    if (!inputCallbackList)
        inputCallbackList = newList();

    pushListHead(inputCallbackList, f);
}

void unregisterKbdListener(void (*f)(struct input)) {
    if (!inputCallbackList)
        return;

    removeFromList(inputCallbackList, f);
}
