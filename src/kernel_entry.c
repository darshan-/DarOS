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
    init_heap(100*1024); // Let's just do 100 KB for now, make 100 MB soon.
    init_idt();
    clearScreen();

    printColor("Ready!\n", 0x0d);
    printf("Let's get some malloc() and free() going...\n");
    void* p1 = malloc(1);
    void* p2 = malloc(1024);
    void* p3 = malloc(512);
    void* p4 = malloc(2048);
    void* p5 = malloc(2048);
    void* p6 = malloc(4096);
    void* p7 = malloc(4096);
    free(p3);
    free(p2);
    free(p1);
    free(p5);
    free(p4);
    free(p6);
    free(p7);

    // printf("Hi, %s okay?  Because, if all is well, 0x%h is hex...\n", "are you", 0x0badface);
    // //printf("Hi, %s okay?  Because, if all is well, 0x%h is hex... And I wuoldd really like this to be long and see if that messes things up....\n", "are you", 0x0badface);
    // printf("....................................................................\n");
    // printf("Hi, %s okay?  Because, if all is well, 0x%4h is hex...\n", "are you", 0x0badface);
    // printf("Hi, %s ___?  Because, if all is well, %0u is badface...\n", "hope", 0x0badfacf);
    // printf("Hi, %s okay?  Because, if all is well, 0x%h is hex...\n", "are you", 0x0badface);
    // printf("This is likely borked, or at least to bork what follows... %u (should be unsigned int)\n", 98764);
    // printf("Hi, %s okay!  Because, if all is well, 0x%h is hex...\n", "I am", 0xfacade);
    // printf("Hi, %s okay!  Because, if all is well, 0x%h is hex...\n", "I *could* be", 0);
    // printf("Hi, %s okay!  Because, if all is well, %u is unsigned...\n", "I *might* be", 12345);

    // //printf("Hi, %s okay!  Because, if all is well, 0x%h is hex...\n", "let's be", 0xa1cafe);
    // printf("Hi, %s okay!  Because, if all is well, 0x%h is hex...\n", "I *could* be", 0);

    com1_print("starting tty\n");
    startTty();
    com1_print("going to waitloop\n");
    waitloop();
}
