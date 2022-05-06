

void kernel_main() {
    char* vram = 0xb8000 + 640 + 640 + 640 + 640 + 640 + 320;
    *vram++ = 0x4f;
    *vram++ = 0xa3;
    *vram++ = 0x6b;
    *vram++ = 0xa3;
}
