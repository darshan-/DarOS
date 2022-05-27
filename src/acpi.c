#include <stdint.h>

#include "acpi.h"
#include "serial.h"

static uint8_t* rsdp = 0;
static uint8_t* rsdt = 0;
static uint8_t* hpet = 0;
static uint8_t* apic = 0;
static uint8_t* facp = 0;

uint64_t* hpet_block = 0;

static uint8_t* find_rsdp() {
    uint64_t rsdp_sig = *((uint64_t*) "RSD PTR ");

    for (uint64_t* p = (uint64_t*) 0x80000; p < (uint64_t*) 0xa0000; p += 2)
        if (*p == rsdp_sig) return (uint8_t*) p;

    for (uint64_t* p = (uint64_t*) 0xe0000; p < (uint64_t*) 0xfffff; p += 2)
        if (*p == rsdp_sig) return (uint8_t*) p;

    return 0;
}

// uint8_t* get_hpet_block() {
//     return hpet_block;
// }

void read_rsdp() {
    if (!(rsdp = find_rsdp())) {
        com1_printf("RSDP signature not found!\n", rsdp);
        return;
    }
        
    uint8_t sum = 0;
    for (int i = 0; i < 20; i++)
        sum += rsdp[i];

    if (sum != 0) {
        com1_print("RSDP checksum failure!\n");
        return;
    }

    rsdt = (uint8_t*)(uint64_t)(*((uint32_t*)(rsdp + 16)));

    uint32_t rsdt_sig = *((uint32_t*) "RSDT");
    if (*((uint32_t*) rsdt) != rsdt_sig) {
        com1_print("RSDT signature failure!\n");
        return;
    }

    uint32_t rsdt_len = *(uint32_t*)(rsdt + 4);
    sum = 0;
    for (uint32_t i = 0; i < rsdt_len; i++)
        sum += rsdt[i];

    if (sum != 0) {
        com1_print("RSDT checksum failure!\n");
        return;
    }

    uint64_t table_count = (rsdt_len - 36) / 4;
    uint32_t *tables = (uint32_t*)(rsdt + 36);

    uint32_t hpet_sig = *((uint32_t*) "HPET");
    uint32_t apic_sig = *((uint32_t*) "APIC");
    uint32_t facp_sig = *((uint32_t*) "FACP");

    for (uint64_t i = 0; i < table_count; i++) {
        uint8_t* table = (uint8_t*)(uint64_t)(tables[i]);

        if (*((uint32_t*) table) == hpet_sig)
            hpet = table;

        if (*((uint32_t*) table) == apic_sig)
            apic = table;

        if (*((uint32_t*) table) == facp_sig)
            facp = table;
    }

    // printf("hpet: %h, apic: %h, facp: %h\n", hpet, apic, facp);

    // printf("hpet is at: %h\n", hpet);

    // uint32_t hpet_len = *(uint32_t*)(hpet + 4);
    // printf("hpet has length: %u\n", hpet_len);

    // uint32_t hpet_addr32 = *(uint32_t*)(hpet + 40);
    // printf("hpet_addr32: %p08h\n", hpet_addr32);

    // uint64_t hpet_addr64 = *(uint64_t*)(hpet + 44);
    // printf("hpet_addr64: %h\n", hpet_addr64);

    // uint32_t hpet_addr32 = *(uint32_t*)(hpet + 40);
    // printf("hpet_addr32: %p08h\n", hpet_addr32);
    // hpet_addr32 = *(uint32_t*)(hpet + 44);
    // printf("hpet_addr32: %p08h\n", hpet_addr32);
    // hpet_addr32 = *(uint32_t*)(hpet + 48);
    // printf("hpet_addr32: %p08h\n", hpet_addr32);

    for (int i = 0; i < 15; i++) {
        uint32_t hpet_addr32 = *(uint32_t*)(hpet + (4*i));
        printf("%p02u: %p08h\n", i, hpet_addr32);
    }

    hpet_block = *(uint64_t**)(hpet + 44);
    // printf("hpet_block: %p016h\n", hpet_block);
    // hpet_block = (uint64_t*)(((uint64_t) hpet_block) << 32);
    // printf("hpet_block: %p016h\n", hpet_block);

    //hpet_block = (uint64_t*)(uint64_t)(*((uint32_t*)(hpet + 44)));
}
