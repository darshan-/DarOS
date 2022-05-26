#include <stdint.h>

static uint8_t* rsdp = 0;
static uint8_t* rsdt = 0;
static uint8_t* hpet = 0;

static uint8_t* find_rsdp() {
    uint64_t rsdptr64 = *((uint64_t*) "RSD PTR ");

    for (uint64_t* p = (uint64_t*) 0x80000; p < (uint64_t*) 0xa0000; p += 2)
        if (*p == rsdptr64) return (uint8_t*) p;
    for (uint64_t* p = (uint64_t*) 0xe0000; p < (uint64_t*) 0xfffff; p += 2)
        if (*p == rsdptr64) return (uint8_t*) p;
    return 0;
}

void read_rsdp() {
    rsdp = find_rsdp();
    printf("rsdp is at 0x%h\n", rsdp);
    uint8_t sum = 0;
    for (int i = 0; i < 20; i++)
        sum += rsdp[i];
    printf("sum: %u\n", sum);
    printf("revision: %u\n", rsdp[15]);
    rsdt = (uint8_t*)(uint64_t)(*((uint32_t*)(rsdp + 16)));
    printf("rsdt should be at: 0x%h\n", rsdt);

    uint32_t rsdt_sig = *((uint32_t*) "RSDT");
    if (*((uint32_t*) rsdt) == rsdt_sig)
        print("rsdt sig matches\n");
    else
        return;

    //uint32_t rsdt_len = *(((uint32_t*) rsdt) + 1);
    uint32_t rsdt_len = *(uint32_t*)(rsdt + 4);
    printf("rsdt has length: %u\n", rsdt_len);
    printf("That leaves room for this many bytes in 32-bit pointer list: %u\n", rsdt_len - 36);
    printf("And thus this many pointers to other tables: %u\n", (rsdt_len - 36) / 4);

    uint64_t table_count = (rsdt_len - 36) / 4;
    uint32_t *dword_pointers = (uint32_t*) (rsdt + 36);

    for (uint64_t i = 0; i < table_count; i++) {
        uint8_t* table = (uint8_t*)(uint64_t)(dword_pointers[i]);
        printf("Table %u should be at: 0x%h\n", i, table);
        uint32_t hpet_sig = *((uint32_t*) "HPET");
        //if (p[0] == 'H' && p[1] == 'P' && p[2] == 'E' && p[3] == 'T')
        if (*((uint32_t*) table) == hpet_sig)
            hpet = table;
    }

    printf("hpet is at: %h\n", hpet);

    uint32_t hpet_len = *(uint32_t*)(hpet + 4);
    printf("hpet has length: %u\n", hpet_len);

    uint32_t hpet_addr32 = *(uint32_t*)(hpet + 40);
    printf("hpet_addr32: %p08h\n", hpet_addr32);

    //uint64_t* hpet_addr64 = (uint64_t*)*((uint64_t*)((uint32_t*) hpet + 11));
    //uint64_t* hpet_addr64 = *(uint64_t*)(uint64_t)((uint32_t*) hpet + 11);
    uint64_t hpet_addr64 = *(uint64_t*)(hpet + 44);
    printf("hpet_addr64: %h\n", hpet_addr64);
}
