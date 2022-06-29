#include <stdint.h>

#include "hpet.h"

#include "acpi.h"
#include "log.h"

#include "../lib/malloc.h"

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
    if (hpet_block == 0) {
        logf("No HPET detected!\n");
        return;
    }

    uint64_t gcir = hpet_block[0];
    logf("General Capabilities and ID Register: 0x%p016h\n", gcir);

    //uint32_t counter_clk_period = (uint32_t) (gcir>>32);
    uint32_t counter_clk_period = gcir >> 32;
    logf("HPET counter increments every %u femptoseconds.\n", counter_clk_period);

    uint8_t leg_rt_cap = !!(gcir & (1<<15));
    logf("leg_rt_cap: %u\n", leg_rt_cap);

    uint8_t num_tim_cap = (gcir & (0b1111<<8)) >> 8; // Index of last timer, so e.g. 2 means there are 3 timers.
    logf("num_tim_cap: %u\n", num_tim_cap);


    uint64_t gcr = hpet_block[2];
    logf("General Configuration Register: 0x%p016h\n", gcr);


    uint64_t gisr = hpet_block[4];
    logf("General Interrupt Status Register: 0x%p016h\n", gisr);


    uint64_t mcvr = hpet_block[30];
    logf("Main Counter Value Register: 0x%p016h\n", mcvr);
}
