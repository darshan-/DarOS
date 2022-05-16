#include <stdarg.h>
#include <stdint.h>
#include "hex.h"
#include "malloc.h"
#include "strings.h"

// struct sfmt {
//     uint64_t val;
//     uint8_t type;
//     int16_t width;
// };

// #define S_STR(s) (struct sfmt) { (uint64_t) s, FMT_TYPE_STR, -1}
// #define FMT_TYPE_STR 1

// void test() {
//     M_sprintf("test %v okay", S_STR("hmm..."));
// }

/*
  Hmm, okay, I think I want something inspired by but different from C's printf.

  %h : uint64_t, displayed as hex (up to caller to use 0x in fmt string if they want it)
  %s : pointer to char (must be zero-terminated string)
  %% : literal '%' character

  I can add more later, like d for signed integers, floating point numbers, etc.


  %8h : print 8 bytes (16 characters)
  %4h : print 4 bytes ( 8 characters)
  %2h : print 2 bytes ( 4 characters)
  %1h : print 1 bytes ( 2 characters)
  %0h : print 1 nibble (1 character)

  Will I ever want padding for strings or decimal numbers, though?  If so, it might be better to go with
    printf style (or closer to it).

  %16h : print 8 bytes (16 characters)  (I guess we can just zero-pad for anything over 16)?
  %7h  : print 3 1/2 bytes (7 characters)
  %1h  : print 1 nibble (1 character)
  etc.

  I guess I can just call QwordToHex for all, and if width specifier is greater than 16, add zeros before
    appending string.  And appending can look the same for everything:
      M_append(s, qs+16-width)
      (So if width is 16, we append qs exactly as we curently do.
       If width is 1, we append qs+15, meaning the last character before the '\0'.
       And it even works for %0h, which in this system means to take up zero characters -- we'll append the
         string starting at '\0', leaving just s, so we're appending nothing.  We'll have copied s for no
         reason, but the default behavior would be correct, so this seems right.

  So, at least for hex, the number between '%' and '%' is how many characters wide to make the hex.

  At some point I'll likely want to space-pad decimal numbers or strings, but this seems good for now.

 */
char* M_sprintf(char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char* s = M_vsprintf(fmt, ap);
    va_end(ap);
    return s;
}

char* M_vsprintf(char* fmt, va_list ap) {
    int scap = 64;
    char* s = malloc(scap);
    char c, *t, *t2;
    int i = 0; // Index into s where we will place next character
    int width;
    char qs[17];
    qs[16] = '\0';

    for (char* p = fmt; *p; p++) {
        if (scap - i < 2) {
            scap *= 2;
            s = realloc(s, scap);
        }

        if (*p != '%') {
            s[i++] = *p;
            continue;
        }

        c = *++p; // Skip past the '%'
        if (c >= '0' && c <= '9') {
        }

        switch (c) {
        case 'u':
            break;
        case 'h':
            qwordToHex(va_arg(ap, uint64_t), qs);
            t2 = qs;
            while(*t2 == '0')
                t2++;
            t = M_append(s, t2);
            free(s);
            s = t;
            i += strlen(t2);

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

    // We made sure above that there was always enough room for this
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
