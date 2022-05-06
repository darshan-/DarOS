#define cd 0xa3;

void kernel_main() {
    prt("C is working great!  Okay!");
}

void prt(char* s) {
    char* vram = 0xb8000 + 640 + 640 + 640 + 640 + 640 + 320;

    while (*s != 0) {
        *vram++ = *s++;
        *vram++ = cd;
    }
}
