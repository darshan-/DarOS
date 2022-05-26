#include <stdint.h>

#include "interrupt.h"
#include "list.h"
#include "malloc.h"
#include "periodic_callback.h"
#include "periodic_callback_int.h"
#include "serial.h"
#include "strings.h"

#define INIT_CAP 10

struct periodic_callbacks periodicCallbacks = {0, 0};

static uint64_t cap = 0;

extern uint64_t int_tick_hz;

void registerPeriodicCallback(struct periodic_callback c) {
    if (c.count < 1 || c.count > int_tick_hz) {
        com1_printf("WARNING: Skipping adding periodic callback with count: %u\n", c.count);
        return;
    }

    no_ints();

    if (!periodicCallbacks.pcs) {
        cap = INIT_CAP;
        periodicCallbacks.pcs = malloc(INIT_CAP * sizeof(void*));
    } else if (periodicCallbacks.len + 1 >= cap) {
        cap *= 2;
        periodicCallbacks.pcs = realloc(periodicCallbacks.pcs, cap * sizeof(void*));
    }

    struct periodic_callback* cp = (struct periodic_callback*) malloc(sizeof(struct periodic_callback));
    cp->count = c.count;
    cp->period = c.period;
    cp->f = c.f;

    periodicCallbacks.pcs[periodicCallbacks.len++] = cp;

    ints_okay();
}

void unregisterPeriodicCallback(struct periodic_callback c) {
    if (!periodicCallbacks.pcs) return;

    no_ints();

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

    ints_okay();
}
