#include <stdint.h>
#include "console.h"
#include "interrupt.h"
#include "keyboard.h"
#include "malloc.h"
#include "serial.h"
#include "strings.h"

// static void dumpMem(uint8_t* start, int count) {
//     char* s = " ";
//     for (int i = 0; i < count; i++) {
//         uint8_t ch = (*start >> 4) + '0';
//         uint8_t cl = (*start++ & 0x0f) + '0';
//         if (ch > '9')
//             ch += 'A' - '9' - 1;
//         s[0] = ch;
//         print(s);
//         if (cl > '9')
//             cl += 'A' - '9' - 1;
//         s[0] = cl;
//         print(s);

//         if (i % 4 == 3) {
//             s[0] = ' ';
//             print(s);
//         }
//         if (i % 16 == 15) {
//             s[0] = '\n';
//             print(s);
//         }
//     }
// }

static void gotChar(char c) {
    printc(c);
}

static void startTty() {
    registerKbdListener(&gotChar);
};

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    init_idt();
    clearScreen();

    printColor("Ready!\n", 0x0d);

    char* s = M_sprintf("Hi, %s okay?  Because, if all is well, 0x%h is hex...\n", "are you", 0x0badface);
    print(s);
    free(s);

    printf("Hi, %s okay!  Because, if all is well, 0x%h is hex...\n", "I am", 0xfacade);
    printf("Hi, %s okay!  Because, if all is well, 0x%h is hex...\n", "I *could* be", 0);
    printf("Hi, %s okay!  Because, if all is well, %u is unsigned...\n", "I *might* be", 12345);

    com1_printf("Hi, %s okay!  Because, if all is well, 0x%10h is hex...\n", "let's be", 0xa1cafe);

    com1_print("starting tty\n");
    startTty();
    com1_print("going to waitloop\n");
    waitloop();
}
