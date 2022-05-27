#include <stdint.h>

#include "acpi.h"
#include "hpet.h"
#include "serial.h"

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
    com1_printf("Using hpet_block: %h\n", hpet_block);
    //uint64_t gcir = *hpet_block;
    uint64_t gcir = hpet_block[0];
    com1_printf("General Capabilities and ID Register: 0x%p016h\n", gcir);
}
