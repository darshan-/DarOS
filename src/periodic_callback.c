#include <stdint.h>
#include "list.h"
#include "malloc.h"
#include "periodic_callback.h"

struct list* periodicCallbackList = (struct list*) 0;

void registerPeriodicCallback(struct periodic_callback c) {
    if (!periodicCallbackList)
        periodicCallbackList = newList();

    struct periodic_callback* cp = (struct periodic_callback*) malloc(sizeof(struct periodic_callback));
    //cp->Hz = c.Hz;
    cp->count = c.count;
    cp->period = c.period;
    cp->f = c.f;

    addToList(periodicCallbackList, cp);
}

void unregisterPeriodicCallback(struct periodic_callback c) {
    if (!periodicCallbackList) return;

    removeFromListWithEquality(periodicCallbackList, ({
        int __fn__ (void* other) {
            return
                //((struct periodic_callback) other)->Hz == c.Hz &&
                ((struct periodic_callback*) other)->count == c.count &&
                ((struct periodic_callback*) other)->period == c.period &&
                ((struct periodic_callback*) other)->f == c.f;
        }

        __fn__;
    }));
}
