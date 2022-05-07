#define VRAM 0xb8000

static char* cur = (char*) VRAM;

void clearScreen() {
    for (long* v = (long*) VRAM; v < (long*) VRAM + (160*25); v++)
        *v = 0x0700070007000700;
}

static void advanceLine() {
    char* vram = (char*) VRAM;

    for (int i=0; i<24; i++)
        for (int j=0; j<160; j++)
            vram[i*160+j] = vram[(i+1)*160+j];
}

void printColor(char* s, char c) {
    while (*s != 0) {
        if (*s == '\n') {
            for (long n = 160 - ((long) cur - VRAM) % 160; n > 0; n -= 2) {
                *cur++ = 0;
                *cur++ = c;
            }
            *s++;
        } else {
            *cur++ = *s++;
            *cur++ = c;
        }

        if (cur >= (char*) VRAM + (160*25)) {
            advanceLine();
            cur = (char*) VRAM + (160*24);
        }
    }
}

void print(char* s) {
    printColor(s, 0x07);
}
