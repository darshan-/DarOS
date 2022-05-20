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
    updateMemUse();
}

static void startTty() {
    registerKbdListener(&gotChar);
};

static void stopTty() {
    unregisterKbdListener(&gotChar);
};

void mTest() {
    com1_print("Entries before malloc():\n");
    dumpEntries(8);
    void* p1 = malloc(1);
    void* p2 = malloc(1024);
    void* p3 = malloc(512);
    void* p4 = malloc(2048);
    void* p5 = malloc(2048);
    void* p6 = malloc(4096);
    void* p7 = malloc(4096);
    com1_print("Entries after malloc():\n");
    dumpEntries(8);
    printf("mem use: %u\n", memUsed());
    //com1_print("Well, that's what that was!\n");
    free(p3);
    free(p2);
    free(p1);
    free(p5);
    free(p4);
    free(p6);
    free(p7);
    com1_print("Entries after free():\n");
    dumpEntries(8);
}

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    init_heap(100*1024); // Let's just do 100 KB for now, make 100 MB soon.
    init_idt();
    uint64_t initBytes = memUsed();
    clearScreen();
    printColor("Ready!\n", 0x0d);
    printf("Let's get some malloc() and free() going...\n");

    mTest();

    printf("Hi, %s okay?  Because, if all is well, 0x%h is hex...\n", "are you", 0x0badface);
    //printf("Hi, %s okay?  Because, if all is well, 0x%h is hex... And I wuoldd really like this to be long and see if that messes things up....\n", "are you", 0x0badface);
    printf("....................................................................\n");
    printf("hi, %s okay?  because, if all is well, 0x%4h is hex...\n", "are you", 0x0badface);
    printf("hi, %s ___?  because, if all is well, %0u is badface...\n", "hope", 0x0badfacf);
    printf("hi, %s okay?  because, if all is well, 0x%h is hex...\n", "are you", 0x0badface);
    printf("this is likely borked, or at least to bork what follows... %u (should be unsigned int)\n", 98764);
    printf("hi, %s okay!  because, if all is well, 0x%h is hex...\n", "i am", 0xfacade);
    printf("hi, %s okay!  because, if all is well, 0x%h is hex...\n", "i *could* be", 0);
    printf("hi, %s okay!  because, if all is well, %u is unsigned...\n", "i *might* be", 12345);

    //printf("hi, %s okay!  because, if all is well, 0x%h is hex...\n", "let's be", 0xa1cafe);
    printf("hi, %s okay!  because, if all is well, 0x%h is hex...\n", "i *could* be", 0);

    com1_print("starting tty\n");
    startTty();
    mTest();

    void* p1 = malloc(2048);
    mTest();
    void* p2 = malloc(1024);
    mTest();
    void* p3 = malloc(129);
    mTest();
    free(p1);
    mTest();
    p1 = malloc(2048);
    mTest();
    free(p1);
    mTest();
    p1 = malloc(1024);
    mTest();
    free(p2);
    mTest();
    p2 = malloc(512);
    mTest();
    free(p2);
    p2 = malloc(7*128);
    mTest();
    free(p3);
    p3 = malloc(17*128);
    mTest();
    free(p2);
    mTest();
    free(p1);
    mTest();
    free(p3);
    mTest();

    printf("mem use: %u bytes\n", memUsed());

    com1_print("stopping tty\n");
    stopTty();
    mTest();

    printf("mem use: %u bytes\n", memUsed());


    com1_print("starting tty\n");
    startTty();
    mTest();

    printf("mem use: %u bytes\n", memUsed());
    printf("init mem use: %uk\n", initBytes);
    com1_print("going to waitloop\n");
    waitloop();
}
