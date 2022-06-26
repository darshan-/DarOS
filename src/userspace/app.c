void main() {
    for (;;)
        asm volatile("inc %r15");
}
