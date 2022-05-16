#include <stdarg.h>
#include <stdint.h>
#include "hex.h"
#include "malloc.h"
#include "strings.h"

/*
  Hmm, okay, I think I want something inspired by but different from C's printf.

  %h : uint64_t, displayed as hex (up to caller to use 0x in fmt string if they want it)
  %s : pointer to char (must be zero-terminated string)
  %% : literal '%' character

  I can add more later, like d for signed integers, floating point numbers, etc.

 */
char* M_sprintf(char* fmt, ...) {
    va_list ap;
    int slen = 64;
    char* s = malloc(slen);
    char* t;
    int i = 0; // Index into s where we will place next character
    char qs[17];
    qs[16] = '\0';

    va_start(ap, fmt);

    for (char* p = fmt; *p; p++) {
        if (slen - i < 2) {
            slen *= 2;
            s = realloc(s, slen);
        }

        if (*p != '%') {
            s[i++] = *p;
            continue;
        }

        switch (*++p) { // Skip past the '%'
        case 'u':
            break;
        case 'h':
            qwordToHex(va_arg(ap, uint64_t), qs);
            t = M_append(s, qs);
            free(s);
            s = t;
            i += 16;

            break;
        case 's':
            char* u = va_arg(ap, char*);

            t = M_append(s, u);
            free(s);
            s = t;
            i += strlen(u);

            break;
        }
    }

    va_end(ap);

    // We made sure above there was always enough room for this
    s[i] = '\0';

    return s;
}

int strlen(char* s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

char* M_append(char* s, char* t) {
    char* u = malloc(strlen(s) + strlen(t));

    char* up = u;
    while(*s)
        *up++ = *s++;
    while(*t)
        *up++ = *t++;

    return u;
}
