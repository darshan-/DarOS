#include <stdint.h>
#include "console.h"
#include "hex.h"
#include "interrupt.h"
#include "keyboard.h"
#include "serial.h"

static void dumpMem(uint8_t* start, int count) {
    char* s = " ";
    for (int i = 0; i < count; i++) {
        uint8_t ch = (*start >> 4) + '0';
        uint8_t cl = (*start++ & 0x0f) + '0';
        if (ch > '9')
            ch += 'A' - '9' - 1;
        s[0] = ch;
        print(s);
        if (cl > '9')
            cl += 'A' - '9' - 1;
        s[0] = cl;
        print(s);

        if (i % 4 == 3) {
            s[0] = ' ';
            print(s);
        }
        if (i % 16 == 15) {
            s[0] = '\n';
            print(s);
        }
    }
}

static void gotChar(char c) {
    //printc(':');
    // if (c == '%')
    //     unregisterKbdListener(&gotChar);
    printc(c);
}

// static void gotChar2(char c) {
//     if (c == '^')
//         unregisterKbdListener(&gotChar2);
//     printc(c);
// }

// static void gotChar3(char c) {
//     if (c == '&')
//         unregisterKbdListener(&gotChar3);
//     printc(c);
// }

// static void startTty3() {
//     registerKbdListener(&gotChar3);
// }

// static void startTty2() {
//     registerKbdListener(&gotChar2);
// }

static void startTty() {
    registerKbdListener(&gotChar);
};

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    printColor("Running 64-bit kernel written in C!\n", 0x0d);

    init_idt();

    dumpKeyboardStaticInits();

    print_com1("init-ed idt\n");

    //void (*printChar)(uint8_t) = consolePrintChar;
    // ccprintChar('H');
    // ccprintChar('i');
    // ccprintChar('?');
    // ccprintChar('\n');

    // printChar('T');
    // printChar('e');
    // printChar('s');
    // printChar('t');
    // printChar('\n');

    // myPrintChar('w');
    // void (*printCharf)(uint8_t) = myPrintChar;
    // //void (*printCharf)(uint8_t) = &printChar;
    // (*printCharf)('h');
    // printCharf('a');
    // //printCharf = printChar;
    // printCharf('t');

    // print("\n");
    // print("0x");
    // hexoutQword(printChar, myPrintChar);
    // print("\n");
    // print("0x");
    // hexoutQword(ptr_printChar, myPrintChar);
    // print("\n");
    // print("0x");
    // hexoutQword(hexoutQword, myPrintChar);
    // print("\n");
    // print("0x");
    // hexoutQword(dumpMem, myPrintChar);
    // print("\n");
    // print("0x");
    // hexoutQword(myPrintChar, myPrintChar);
    // print("\n");

    // print("--\n");
    // printConsoleFuncs();
    // //hexoutQword(0x5037527165facead, printChar);
    // //hexoutByte(0x19, &printChar);

    // dumpMem(0, 16*40);

    clearScreen();

    char qs[17];
    qs[16] = '\0';

    print("test: 0x");
    qwordToHex(0xfa15a324e98732ca, qs);
    print(qs);
    print("\n");

    char* t2 = "&gotChar: 0x0000000000000000\n";
    qwordToHex(&gotChar, &t2[12]);
    print(t2);

    char* t3 = "&printc: 0x0000000000000000\n";
    qwordToHex(&printc, &t3[11]);
    print(t3);

    printPrintc();

    void(*fakePrintc)(char c) = 0x0aa7;
    char* t4 = "&frintc: 0x0000000000000000\n";
    qwordToHex(fakePrintc, &t4[11]);
    print(t4);

    //fakePrintc('#');

    print_com1("printing ready\n");

    printColor("Ready!\n", 0x0d);
    print_com1("starting tty\n");
    startTty();
    print_com1("going to waitloop\n");
    // startTty2();
    // startTty3();
    waitloop();

    //waitloop();

    //__asm__("int $3");
    //__asm__("int $3");

    //print("\n\n\n\n\n                  ");
    //printColor("Cool!", 0x3f);
    //for(int i=0; i<25; i++) print("0123456789abcdefghijklmnopqrstuvwxyz!\n");
    //print("Hi!\n");
    //print("01234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
    //print("01234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
}
