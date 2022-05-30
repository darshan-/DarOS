#include <stdint.h>

#include "acpi.h"
#include "hpet.h"
#include "malloc.h"
#include "serial.h"


#include "console.h"
#define com1_printf printf

/*
  000-007h RO General Capabilities and ID Register
  010-017h RW General Configuration Register
  020-027h RW General Interrupt Status Register
  0f0-0f7h RW Main Counter Value Register
  100-107h RW Timer 0 Configuration and Capability Register
  108-10fh RW Timer 0 Comparator Value Register
  110-117h RW Timer 0 FSB Interrupt Route Register
  120-127h RW Timer 1 Configuration and Capability Register
  128-12fh RW Timer 1 Comparator Value Register
  130-137h RW Timer 1 FSB Interrupt Route Register
  140-147h RW Timer 2 Configuration and Capability Register
  148-14fh RW Timer 2 Comparator Value Register
  150-157h RW Timer 2 FSB Interrupt Route Register
*/

void init_hpet() {
    com1_print("hpet01\n");
    if (hpet_block == 0) {
        com1_printf("hpet_block is not set!n");
        return;
    }
    com1_print("hpet02\n");

    // Presario: 0x0008_8E12_0000_0000
    // Presario: 0x0008_8EBA_0000_0000
    /*
      51   47   43   39   35   31   27   23   19   15   11   7    3
      1000_1000_1110_0001_0010_0000_0000_0000_0000_0000_0000_0000_0000

      Bits 47:39 index into the 512-entry page-map level-4 table.
      Bits 38:30 index into the 512-entry page-directory-pointer table.
      Bits 29:21 index into the 512-entry page-directory table.
      Bits 20:0 provide the byte offset into the physical page.

      l4: 1000_1110_0
      l3: 001_0010_00
      l2: 00_0000_000


      So I think lie, and subtract 001_0010_00 from l3 address, and put that in l4[1000_1110_0]?

      Except that it changed again (I guess it's loaded to somewhere random(ish) within a range?)
      8EB8 this time.

      So do the math dynamically.

     */
    // Dynamically make a l2 page for that?
    com1_printf("Using hpet block at 0x%h\n", hpet_block);

    if ((uint64_t) hpet_block >= (512ull * 1024 * 1024 * 1024)) {
        com1_print("hpet_block is not in addressable region; let's make a new page table...\n");
        // Let's try a hacky approach to adding an l2 table for this and see if it works; come up
        //  with something cleaner and better if so.

        /*
          PT_PRESENT equ 1
          PT_WRITABLE equ 1<<1
          PT_HUGE equ 1<<7

          page_table_l4 equ 0x1000
         */

        // //uint64_t* l2 = (uint64_t*) (0x0100000ull + (512ull * 8 * 10));
        // uint64_t* l2 = malloc(sizeof(uint64_t*));
        // uint64_t* l3 = malloc(sizeof(uint64_t*));
        // uint64_t* l4 = (uint64_t*) 0x1000;

        // // TODO: Is it possible for block to be near end of 2 MB page? I should check, and make a second page if so?
        // //  (or unconditionally do to, kinda paranoid, but maybe necessary sometimes?)
        // *l2 = (((uint64_t) hpet_block) & (~0xfffff)) | 1 | 1<<1 | 1<<7;
        // *l3 = ((uint64_t) l2) | 1 | 1<<1;

        // //l4[1] = (uint64_t) l3;
        // l4[(((uint64_t) hpet_block >> 39ull) & 0b111111111)] = (uint64_t) l3 - (((uint64_t) hpet_block >> 30ull) & 0b111111111);
        // com1_printf("*l2: %h\n", *l2);

        // uint64_t l4_index = (((uint64_t) hpet_block >> 39ull) & 0b111111111);
        // com1_printf("l4_index: %u; l4[%u] = 0x%h\n", l4_index, l4_index, l4[l4_index]);
        // com1_printf("l3: 0x%h; *l3: 0x%h\n", l3, *l3);

        uint64_t* l3 = (uint64_t*) 0x200000;
        uint64_t* l2 = (uint64_t*) 0x201000;
        uint64_t* l4 = (uint64_t*) 0x1000;

        *l2 = (((uint64_t) hpet_block) & (~0xfffff)) | 1 | 1<<1 | 1<<7;
        l3[(((uint64_t) hpet_block >> 30ull) & 0b111111111)] = (uint64_t) l2 | 1 | 1<<1;
        l4[(((uint64_t) hpet_block >> 39ull) & 0b111111111)] = (uint64_t) l3 | 1 | 1<<1;

        uint64_t l4_index = (((uint64_t) hpet_block >> 39ull) & 0b111111111);
        uint64_t l3_index = (((uint64_t) hpet_block >> 30ull) & 0b111111111);
        com1_printf("l4_index: %u; l4[%u]: 0x%h\n", l4_index, l4_index, l4[l4_index]);
        com1_printf("l3_index: %u; l3[%u]: 0x%h\n", l3_index, l3_index, l3[l3_index]);
        com1_printf("l2[0]: 0x%h\n", l2[0]);
    }

    uint64_t gcir = hpet_block[0];
    com1_printf("General Capabilities and ID Register: 0x%p016h\n", gcir);

    com1_print("hpet03\n");

    //uint32_t counter_clk_period = (uint32_t) (gcir>>32);
    uint32_t counter_clk_period = gcir >> 32;
    com1_printf("HPET counter increments every %u femptoseconds.\n", counter_clk_period);

    uint8_t leg_rt_cap = !!(gcir & (1<<15));
    com1_printf("leg_rt_cap: %u\n", leg_rt_cap);

    uint8_t num_tim_cap = (gcir & (0b1111<<8)) >> 8; // Index of last timer, so e.g. 2 means there are 3 timers.
    com1_printf("num_tim_cap: %u\n", num_tim_cap);


    uint64_t gcr = hpet_block[2];
    com1_printf("General Configuration Register: 0x%p016h\n", gcr);


    uint64_t gisr = hpet_block[4];
    com1_printf("General Interrupt Status Register: 0x%p016h\n", gisr);


    uint64_t mcvr = hpet_block[30];
    com1_printf("Main Counter Value Register: 0x%p016h\n", mcvr);
}
