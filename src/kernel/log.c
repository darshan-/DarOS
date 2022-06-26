#include <stdarg.h>

#include "log.h"

#include "console.h"
#include "list.h"
#include "strings.h"

static struct list* logs = (struct list*) 0;

void log(char* s) {
    if (!logs)
        logs = newList();

    // TODO: Include timestamp of log.
    // I could just do an int64_t and format when showing, but I think I like formatting now and just storing
    //   the string?
    pushListTail(logs, M_scopy(s));

    printTo(LOGS_TERM, s);
}

void logf(char* fmt, ...) {
    VARIADIC_PRINT(log);
}

void* forEachLog(void (*f)(char*)) {
    return forEachListItem(logs, ({
        void __fn__ (void* s) {
            f((char*) s);
        }
        __fn__;
    }));
}

void* forEachNewLog(void* last, void (*f)(char*)) {
    return forEachNewListItem(last, ({
        void __fn__ (void* s) {
            f((char*) s);
        }
        __fn__;
    }));
}
