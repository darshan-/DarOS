#include <stdint.h>

#include "sys.h"

// I think I want to soon make strings library a single thing importatable by the kernel or userspace.
// Malloc still seems easiest kept separate.
// But then my print functions can stop being variadic, and less work is done in the kernel for printf.

void processInput(char* l) {
    if (!strcmp(l, "app"))
        runProg("app");
}

void main() {
    for (;;) {
        printColor("\3 > ", 0x05);
        char* l = M_readline();
        processInput(l);
        free(l);
    }   
}
