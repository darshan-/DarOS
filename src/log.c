#include <stdarg.h>

#include "log.h"

#include "list.h"
#include "strings.h"

static struct list* logs = (struct list*) 0;

void log(char* s) {
    if (!logs)
        logs = newList();

    pushListTail(logs, M_scopy(s));
}

void logf(char* fmt, ...) {
    VARIADIC_PRINT(log);
}

void forEachLog(void (*f)(char*)) {
    forEachListItem(logs, ({
        void __fn__ (void* s) {
            f((char*) s);
        }
        __fn__;
    }));
}
