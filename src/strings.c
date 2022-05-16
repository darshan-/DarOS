#include <stdarg.h>
#include <stdint.h>
#include "malloc.h"
#include "strings.h"

/*
  Hmm, okay, I think I want something inspired by but different from C's printf.

  B specifies how many bytes, must be present where listed here, and one of 1, 2, 4, or 8

  %uB : uintX_t, displayed as decimal
  %hB : uintX_t, displayed as hex (up to caller to use 0x in fmt string if they want it)
  %s : pointer to char (must be zero-terminated string)
  %% : literal '%' character

  I can add more later, like d for signed integers, floating point numbers, etc.

  What shall I do in case of bad format?  E.g. Something other than 'u', 'h', 's', or '%' after '%'?  Or something
    other than '1', '2', '4', or '8' after 'u' or 'h'?

 */
char* M_sprintf(char* fmt, ...) {
    va_list ap;
    int slen = 2; // For testing; default to something much higher once I know it works, likely 128 or so
    //int sloc = 0; //
    char* s = malloc(slen);
    //char* t = s; // Next location in *s to place another character
    int i = 0; // Index into s where we will place next character
    uint8_t u1;
    uint16_t u2;
    uint32_t u4;
    uint64_t u8;

    va_start(ap, fmt);

    for (char* p = fmt; *p; p++) {
        int b;
        char c;

        if (slen - i < 2) {
            slen *= 2;
            s = realloc(s, slen);
        }

        if (*p != '%') {
            s[i++] = *p;
            continue;
        }

        // *p is '%'
        c = *++p;
        switch (c) {
        case 'h':
        case 'u':
            // if (*p < '0' || *p > '9') {
            //     s[i++] = c;
            //     s[i++] = *p;
            //     continue;
            // }
            // b = c + '0';
            switch (*p) {
            case '1':
            case '2':
            case '4':
            case '8':
                b = c + '0';
                break;
            deafult:
                s[i++] = c;
                s[i++] = *p;
                continue;
            }
            break;
        }

        switch (c) {
        case 'u':
            break;
        case 'h':
            switch (b) {
            case 1:
                u1 = va_arg(ap, uint8_t);
                break;
            }
            break;
        case 's':
            // malloc and copy -- so need strlen and strcopy
            break;
        }
    }

    va_end(ap);
    // Now make sure there's room (ooh, or have lengthening part of loop make sure there's always room for two
    //   characters, the one we're about to add and a 0
    //*t = '\0';
    s[i] = '\0';

    return s;
}
