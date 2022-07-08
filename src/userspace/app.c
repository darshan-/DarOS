#include <stdint.h>

#include "sys.h"

static uint64_t a;
static uint64_t b;

static void fibNext() {
    uint64_t c = a;
    a = b;
    b = a + c;
}

void main() {
    a = 1;
    b = 1;

    while (b < 10000000000000000000ull)
        fibNext();

    printf("Hi, I'm app; I've stopped fib-ing with a: %u and b: %u\n", a, b);

    print("Say something, please? ");
    char* l = M_readline();
    if (l) {
        printf("  Thanks for saying \"%s\"\n", l);
        free(l);
    }

    const uint64_t chunk = 1000000000ull / 4; // Chunk that's not too fast, not too slow, for target system
    a = 0;

    for (; a < chunk; a++)
        if (a % (chunk / 10) == 0)
            printf("a: %u\n", a);
    print("Ending without a newline.");
}
