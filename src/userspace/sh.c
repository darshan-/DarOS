#include <stdint.h>

#include "sys.h"

static void prompt(uint64_t t) {
    terms[at].cur = terms[at].end; // Call cursorEnd?  How much is terminal and how much is shell?
    if (terms[t].cur % 160 != 0)
        printTo("\n");

    printColorTo("\3 > ", 0x05);
}

void main() {
}
