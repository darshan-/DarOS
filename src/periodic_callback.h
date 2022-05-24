#pragma once

#include "list.h"

struct periodic_callback {
    uint64_t count;
    uint64_t period;
    void (*f)();
};

void registerPeriodicCallback(struct periodic_callback c);
void unregisterPeriodicCallback(struct periodic_callback c);
