#pragma once

#include "list.h"

extern struct list* periodicCallbackList;

struct periodic_callback {
    //double Hz; // Ah, I was right to be wary of floating point in kernel... SSE registers, etc.
    uint64_t count;
    uint64_t period;
    void (*f)();
};

void registerPeriodicCallback(struct periodic_callback c);
void unregisterPeriodicCallback(struct periodic_callback c);
