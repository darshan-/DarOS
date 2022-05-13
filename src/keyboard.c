#include <stdint.h>
#include "console.h"
#include "keyboard.h"

void keyScanned(uint8_t c) {
    if (c == 0x20) printc('D');
}
