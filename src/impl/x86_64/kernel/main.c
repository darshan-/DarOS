#include "console.h"

//#define cd 0xa3;

//void prt(char* s) {
//    char* vram = (char*) 0xb8000;// + 640 + 640 + 640 + 640 + 640 + 320;
//
//    while (*s != 0) {
//        *vram++ = *s++;
//        *vram++;// = 0x07;
//    }
//}

//void clear_screen() {
    //for (char* v = (char*) 0xb8000; v < (char*) 0xb8000 + (160*25); v++)
    //    *v = 0;
//}

void printForever() {
    char* s = "0";

    for(int i=0; i < 1000*1000+17; i++) {
        if (*s == '7') *s = '0';
        print(s);
        (*s)++;
    }
}

void kernel_main() {
    clearScreen();
    print("C is working great!\n  Okay!\n");
    //return;

    /* for (int i = 0; i < 10; i++) */
    /*     print("10\n"); */
    /* for (int i = 0; i < 10; i++) */
    /*     print("20\n"); */
    /* for (int i = 0; i < 10; i++) */
    /*     print("30\n"); */

    /* print("And now we're down here!"); */

    //printForever();

    print("\n\n\n\n      ");
    printColor("Hi, Asha!!!!!!", 0x53);
    print("\n\n");
}
