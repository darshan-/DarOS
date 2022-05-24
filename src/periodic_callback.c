#include <stdint.h>
#include "list.h"
#include "malloc.h"
#include "periodic_callback.h"

// Zero-terminated array of pointers to callback structs
struct periodic_callback *periodicCallbacks = 0;

static uint64_t cap = 0, len = 0;


// If we're at capacity, realloc

void registerPeriodicCallback(struct periodic_callback c) {
    __asm__ __volatile__("cli");

    if (!periodicCallbacks) {
        cap = INIT_CAP;
        periodicCallbacks = malloc(INIT_CAP * sizeof(void*));
    } else if (len + 1 >= cap) {
        periodicCallbacks = realloc(periodicCallbacks, cap * 2);
    }

    struct periodic_callback* cp = (struct periodic_callback*) malloc(sizeof(struct periodic_callback));
    cp->count = c.count;
    cp->period = c.period;
    cp->f = c.f;

    // Add cp to callbacks

    __asm__ __volatile__("sti");
}

void unregisterPeriodicCallback(struct periodic_callback c) {
    if (!periodicCallbacks) return;

    __asm__ __volatile__("cli");

    __asm__ __volatile__("sti");

    // Remove callback that matches c from callbacks
    // removeFromListWithEquality(periodicCallbackList, ({
    //     int __fn__ (void* other) {
    //         return
    //             //((struct periodic_callback) other)->Hz == c.Hz &&
    //             ((struct periodic_callback*) other)->count == c.count &&
    //             ((struct periodic_callback*) other)->period == c.period &&
    //             ((struct periodic_callback*) other)->f == c.f;
    //     }

    //     __fn__;
    // }));
}
