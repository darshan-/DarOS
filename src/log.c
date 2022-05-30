#include <stdarg.h>

#include "log.h"

#include "list.h"
#include "strings.h"

static struct list* logs = (struct list*) 0;

void log(char* s) {
    if (!logs)
        logs = newList();

    addToList(logs, M_scopy(s));
}

void logf(char* fmt, ...) {
    VARIADIC_PRINT(log);
}
