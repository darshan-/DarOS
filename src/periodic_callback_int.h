#pragma once

#include "periodic_callback.h"

struct periodic_callbacks {
    struct periodic_callback** pcs;
    uint64_t len;
};

extern struct periodic_callbacks periodicCallbacks;
