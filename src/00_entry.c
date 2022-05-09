#include <stdint.h>
#include "console.h"

#define VRAM 0xb8000

void print_at();
void print_percent();

// NOTE: With my current setup, I need my entry point to come first after linking, so:
//  1. Make sure this file comes first alphabetically (hence 00_ at start of file name) (ld generates
//     output in order of files listed on command line, and we use a glob to capture all object files for
//     linking, so it's kludgy but simple and effective), and
//  2. Make sure not to define any functions before kernel_entry() (so declare any helper functions
//     above and define them after kernel_entry().
//  It may be straightforward to have linker script put kernel_entry first, but ENTRY(kernel_entry) doesn't
//   do it for some reason, at least with OUTPUT_FORMAT("binary"), even though various sources suggest it
//   should.  So for now, the kludge.

void kernel_entry() {
    print_at();
    print_percent();
    //clearScreen();
    //print("Kernel launched!\n");
    //return;

    uint8_t* loc = (uint8_t*) VRAM + 640 + 2;
    *loc++ = 's';
    loc++;
    *loc++ = 'm';

    printColor("Full C kernel launched!\n", 0x0d);


    print("\n\n('@' printed by inline assembly!)");

    print("\n\n");
    printColor("And things are moving forward!", 0x12);

    //print("\n\n\n\n\n                  ");
    //printColor("Cool!", 0x3f);
    //for(int i=0; i<25; i++) print("0123456789abcdefghijklmnopqrstuvwxyz!\n");
    //print("Hi!\n");
    //print("01234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
    //print("01234567890123456789012345678901234567890123456789012345678901234567890123456789\n");
}

void print_at() {
    __asm__(
        "movl $0xb8000+640, %eax\n"
        "movb $0x40, %cl\n"
        "movb %cl, (%eax)\n"
        );
}

void print_percent() {
    uint8_t* loc = (uint8_t*) VRAM + 640 + 160;
    *loc = '%';
}

/*
  Delete to 400 works
  Delete to 440 does not
  ...


  Delete to 0x404 and it boots fine...
  Leave one more byte at 0x405 and it bootloops...


  Hmmm, and same deal even with extra code at end of bootloader (to see if anything is run after hlt).

  But none of it should ever run...
  Okay, but is it being loaded over something?  It kind of must be, right???

  That's 0x8004, I believe...

  Hmmm, just past the end of the second sector?

  Yeah, 0x400 is 1024, or two full 512-byte sectors...

  ...

  Yep, and this led to me figuring out how the bss section works and sort things out!

  Like, still a bit to figure out (why can't I make it big?), but we're able to move forward with development,
    and have everything make sense.
  
 */
