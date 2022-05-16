#pragma once

#include <stdarg.h>
#include "strings.h"

char* M_sprintf(char* fmt, ...);
char* M_vsprintf(char* fmt, va_list ap);
int strlen(char* s);
char* M_append(char* s, char* t);
