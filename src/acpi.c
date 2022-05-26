#include <stdint.h>

static uint64_t* rsdp = 0;
static uint64_t* rsdt = 0;
static uint64_t* hpet = 0;

static uint64_t* find_rsdp() {
    // uint16_t* x040e = (uint16_t*) 0x040e;
    // uint16_t* x0413 = (uint16_t*) 0x0413;
    // printf("0x040e has: %p04h\n", *x040e);
    // printf("0x0413 has: %p04h\n", *x0413);

    uint64_t rsdptr64 = *((uint64_t*) "RSD PTR ");

    for (uint64_t* p = (uint64_t*) 0x80000; p < (uint64_t*) 0xa0000; p += 2)
        if (*p == rsdptr64) return p;
    for (uint64_t* p = (uint64_t*) 0xe0000; p < (uint64_t*) 0xfffff; p += 2)
        if (*p == rsdptr64) return p;
    return 0;
}

void read_rsdp() {
    rsdp = find_rsdp();
    printf("rsdp is at 0x%h\n", rsdp);
    // uint8_t checksum = *((uint8_t*) rsdp+8);
    // printf("checksum is %u\n", checksum);
    uint8_t* rsdp_bytes = (uint8_t*) rsdp;
    uint8_t sum = 0;
    for (int i = 0; i < 20; i++)
        sum += rsdp_bytes[i];
    printf("sum: %u\n", sum);
    // char* oemid = rsdp_bytes + 9;
    // char* oemid0 = "123456";
    // printf("oemid: %s\n", oemid);
    uint8_t revision = rsdp_bytes[15];
    printf("revision: %u\n", revision);
    //uint32_t rsdt32 = (uint32_t*)(rsdp_bytes + 16)
    rsdt = (uint64_t*)(uint64_t)(*((uint32_t*)(rsdp_bytes + 16)));
    printf("rsdt should be at: 0x%h\n", rsdt);

    // Is that right?  Let's see what's there...

    char* p = (char*) rsdt;
    for (int i = 0; i < 4; i++)
        printc(p[i]);
    print("\n");

    // Okay, cool!

    uint32_t rsdt_len = *(((uint32_t*) rsdt) + 1);
    printf("rsdt has length: %u\n", rsdt_len);
    printf("That leaves room for this many bytes in 32-bit pointer list: %u\n", rsdt_len - 36);
    printf("And thus this many pointers to other tables: %u\n", (rsdt_len - 36) / 4);

    uint64_t table_count = (rsdt_len - 36) / 4;
    uint32_t *dword_pointers = ((uint32_t*) rsdt) + 9;

    for (uint64_t i = 0; i < table_count; i++) {
        uint64_t* table = (uint64_t*)(uint64_t)(dword_pointers[i]);
        printf("Table %u should be at: 0x%h\n", i, table);
        print("It should be called: ");
        p = (char*) table;
        for (int i = 0; i < 4; i++)
            printc(p[i]);
        print("\n");

        if (p[0] == 'H' && p[1] == 'P' && p[2] == 'E' && p[3] == 'T')
            hpet = table;
    }

    printf("hpet is at: %h\n", hpet);

    uint32_t hpet_len = *((uint32_t*) hpet + 1);
    printf("hpet has length: %u\n", hpet_len);
}
