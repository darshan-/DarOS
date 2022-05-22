#include <stdint.h>
#include "console.h"
#include "interrupt.h"
#include "keyboard.h"
#include "malloc.h"
#include "rtc.h"
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
    updateMemUse();
}

static void startTty() {
    registerKbdListener(&gotChar);
    updateMemUse();
};

// static void stopTty() {
//     unregisterKbdListener(&gotChar);
// };

// void mTest() {
//     com1_print("Entries before malloc():\n");
//     dumpEntries(8);
//     void* p1 = malloc(1);
//     void* p2 = malloc(1024);
//     void* p3 = malloc(512);
//     void* p4 = malloc(2048);
//     void* p5 = malloc(2048);
//     void* p6 = malloc(4096);
//     void* p7 = malloc(4096);
//     com1_print("Entries after malloc():\n");
//     dumpEntries(8);
//     printf("mem use: %u\n", memUsed());
//     //com1_print("Well, that's what that was!\n");
//     free(p3);
//     free(p2);
//     free(p1);
//     free(p5);
//     free(p4);
//     free(p6);
//     free(p7);
//     com1_print("Entries after free():\n");
//     dumpEntries(8);
// }

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    init_heap(100*1024);
    init_idt();

    clearScreen();
    printColor("Ready!\n", 0x0d);

    com1_print("starting tty\n");
    startTty();

    com1_print("going to waitloop\n");
    waitloop();
}
