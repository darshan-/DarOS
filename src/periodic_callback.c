#include <stdint.h>
#include "list.h"
#include "malloc.h"
#include "periodic_callback.h"
#include "periodic_callback_int.h"
#include "strings.h"

#define INIT_CAP 10

struct periodic_callbacks periodicCallbacks = {0, 0};

static uint64_t cap = 0;

void registerPeriodicCallback(struct periodic_callback c) {
    __asm__ __volatile__("cli");

    if (!periodicCallbacks.pcs) {
        cap = INIT_CAP;
        periodicCallbacks.pcs = malloc(INIT_CAP * sizeof(void*));
    } else if (periodicCallbacks.len + 1 >= cap) {
        cap *= 2;
        periodicCallbacks.pcs = realloc(periodicCallbacks.pcs, cap);
    }

    struct periodic_callback* cp = (struct periodic_callback*) malloc(sizeof(struct periodic_callback));
    cp->count = c.count;
    cp->period = c.period;
    cp->f = c.f;

    periodicCallbacks.pcs[periodicCallbacks.len++] = cp;

    __asm__ __volatile__("sti");
}

void dumpPCs() {
    for (int i = 0; i < periodicCallbacks.len; i++) {
        com1_printf("{{{{i: %u, (%u, %u)\n", i, periodicCallbacks.pcs[i]->period, periodicCallbacks.pcs[i]->count);
        // com1_printf("@ 0x%h;", periodicCallbacks.pcs[i]);
    }
}

void plumpPCs() {
    char buf[17];
    buf[16] = 0;
    for (int i = 0; i < periodicCallbacks.len; i++) {
        int p = periodicCallbacks.pcs[i]->period;
        int c = periodicCallbacks.pcs[i]->count;
        if (p > 1000 || c > 1000) {
            qwordToHex(p, buf);
            com1_print(buf);
            com1_print(", ");
            qwordToHex(c, buf);
            com1_print(buf);
        }
        // qwordToHex(periodicCallbacks.pcs[i]->period, buf);
        // com1_print(buf);
        // com1_print(", ");
        // qwordToHex(periodicCallbacks.pcs[i]->count, buf);
        // com1_print(buf);
        // com1_print("\n");
    }
    com1_print("\n");
}

void unregisterPeriodicCallback(struct periodic_callback c) {
    if (!periodicCallbacks.pcs) return;
    com1_print("unregistering");

    __asm__ __volatile__("cli");

    int found = 0;
    for (uint64_t i = 0; i < periodicCallbacks.len - 1; i++) {
        if (found) {
            periodicCallbacks.pcs[i] = periodicCallbacks.pcs[i+1];
        } else if (periodicCallbacks.pcs[i]->count == c.count &&
                   periodicCallbacks.pcs[i]->period == c.period &&
                   periodicCallbacks.pcs[i]->f == c.f) {
            found = 1;
        }
    }

    periodicCallbacks.len--;

    __asm__ __volatile__("sti");
}
