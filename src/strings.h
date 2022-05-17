#pragma once

#include <stdarg.h>
#include "strings.h"

char* M_sprintf(char* fmt, ...);
char* M_vsprintf(char* fmt, va_list ap);
int strlen(char* s);
char* M_append(char* s, char* t);

#define VARIADIC_PRINT(p) va_list ap; \
    va_start(ap, fmt); \
    char* s = M_vsprintf(fmt, ap); \
    va_end(ap); \
    p(s); \
    free(s)
