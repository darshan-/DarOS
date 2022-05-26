#include <stdint.h>
#include "console.h"
#include "cpuid.h"
#include "interrupt.h"
#include "keyboard.h"
#include "malloc.h"
#include "serial.h"
#include "strings.h"

static void gotChar(char c) {
    printc(c);
}

static void startTty() {
    registerKbdListener(&gotChar);
};

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    init_heap(100*1024*1024);
    init_interrupts();

    clearScreen();
    printColor("Ready!\n", 0x0d);

    com1_print("starting tty\n");
    startTty();

    // char demo[255];
    // for (uint8_t i = 0; i < 254; i++)
    //     demo[i] = (char) i+1;
    // print(demo);

    struct cpuid_ret r = cpuid(0);
    char manufId[13];
    manufId[12] = 0;
    uint32_t* p = (uint32_t*) manufId;
    *p++ = r.ebx;
    *p++ = r.edx;
    *p = r.ecx;
    printf("cpuid with eax 0x00000000 returns %p08h in eax and man. ID: %s\n", r.eax, manufId);

    r = cpuid(0x80000000ul);
    printf("cpuid with eax 0x80000000 returns %p08h in eax\n", r.eax);

    r = cpuid(0x80000001ul);
    printf("cpuid with eax 0x80000001 returns %p08h in edx and %p08h in ecx\n", r.edx, r.ecx);

    r = cpuid(0x80000007ul);
    printf("cpuid with eax 0x80000007 returns %p08h in edx\n", r.edx);

    updateMemUse();

    //unmask_pics();
    com1_print("going to waitloop\n");
    waitloop();
}
