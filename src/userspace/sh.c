#include <stdint.h>

#include "sys.h"
#include "../lib/strings.h"

void processInput(char* l) {
    if (l[0] == 0) // Just re-prompt again if empty input (just pressed enter at prompt)
        return;
    else if (!strcmp(l, "exit"))
        exit();
    else if (!strcmp(l, "help"))
        print("I'm not very helpful, but I hope you have a nice day!\n");
    else {
        uint64_t p = runProg(l);

        if (p)
            wait(p);
        else
            printf("Unknown command: '%s'\n", l);
    }
}

void main() {
    char* s = M_sprintf(" (#%u)\n", stdout);
    printColor("Ready!", 0x0d);
    printColor(s, 0x0b);
    free(s);

    for (;;) {
        printColor("\r\3 > ", 0x05);
        char* l = M_readline();
        processInput(l);
        free(l);
    }   
}
