#include <stdint.h>

#include "acpi.h"
#include "console.h"
#include "hpet.h"
#include "interrupt.h"
#include "log.h"
#include "malloc.h"
#include "serial.h"
#include "strings.h"

struct mem_table_entry {
    uint64_t start : 64;
    uint64_t length : 64;
    uint64_t type : 64;
};

#define STACK_SIZE (64 * 1024)

/*
  Overall to-do list:

  * User space
    - Need a user-mode segment in GDT, but I've been conflating GDT and page tables -- set new l4 table in cr3 as part of
      switch to user mode.  Not new GDT, but new l4 table.  Not new ldtr, but new cr3.
      We need to set the U/S bit (1<<2) on all three levels.
      We only make entries for valid addresses.  So first attempt would pick a single 2MB of memory (2MB-aligned) to be for
        my single user-space task, and wherever we put that, we... Oh, the point is paging.  So we can put that as the first
        and single entry of an l2 table, right?  And then the process will see itself as having flat memory from 0 to 2MB, and
        the entry tells the processsor where it is in physical memory.
  * Shell
  * HPET -- not just for finer resolution of timer, but as source of actual timer count -- missed ticks are less bad.
  * Network?
  * File system?
  * Graphics? (At least for my own text mode -- better/more colors, custom font, etc.?)

 */

/*

  Okay, so to set up a single user-mode page, I need 3 4k pages for tables, plus the 2 MB page itself.
  The user-mode code needs to be either entirely local references, or know where it's going to end up.
  Hmm, for this first experiment, trying for simplest and easiest test possible (well, that I can easily think of), I should be
    able to have it share the same identity-mapped address space.  It's just that they're using different page tables, and the
    user-space page tables a) are marked with u/s bit, and b) only have one page identity mapped -- the other should be null
    mapped.  (Mapping all other pages to the null descriptor only occured to me while writing the last sentence, and seems
    obviously what I'm meant to do, I think.)  Null-mapping means I flesh out all of l4 and l3 with null descriptors too, not
    just l2.  But all zeros makes a null-descriptor, I think, so getting mallocz to give my arbitrarily aligned memory (or just
    malloczing a region twice as large as I want to start at the aligned place myself for now) should be a workable approach for
    this first test.

  Yeah, AMD manual says "When the P bit is 0, indicating a not-present page, all remaining bits in the page data-structure entry
    are available to software," which seems to mean I can have the whole thing be zeroed, and that's valid, and means a fault will
    occur if user-mode code tries to access memory for that page.

 */

#define PT_PRESENT  1
#define PT_WRITABLE 1 << 1
#define PT_USERMODE 1 << 2
#define PT_HUGE     1 << 7

void userMode() {
    for(;;)
        __asm__ __volatile__ ("hlt\n");
}

void setUpUserMode() {
    // void* tables = mallocz(1024 * 4 * 3 * 2); // 3 4K tables, doubled so I can clunkily align
    // //void* l4 = tables - (uint64_t) tables % 0x1000;
    // void* l4 = (void*) ((uint64_t) tables & ~0xfff);
    // void* l3 = l4 + 0x1000;
    // void* l2 = l3 + 0x1000;

    //void* stack = malloc(1024 * 8);
    uint8_t* u = malloc(2 * 1024 * 1024 * 2); // 2MB page, doubled so I can clunkily align
    u = (uint8_t*) ((uint64_t) u & ~(1024 * 1024 * 4 - 1));
    void* stack_top = u + 2 * 1024 * 1024 - 1024;

    uint8_t* uc = (uint8_t*) &userMode;
    for (uint64_t i = 0; i < 512; i++)
        u[i] = uc[i];

    uint64_t* tables = mallocz(1024 * 4 * 3 * 2); // 3 4K tables, doubled so I can clunkily align
    uint64_t* l4 = (uint64_t*) ((uint64_t) tables & ~0xfff);
    uint64_t* l3 = l4 + 0x1000 / 8;
    // uint64_t* l2 = l3 + 0x1000 / 8;
    uint64_t* l2 = (uint64_t*) 0x100000;

    // Okay, but not 0 for each of those, but rather, work out the l4 index, l3 index, and l2 index for where u actually is

    // Hmm, so, let's see.  I think each entry in l2 represents 2 MB, so each entry in l3 represents 512 * 2 MB, or 1 GB.
    // Which is familiar from the early days when that's all we mapped.
    // So for this experiment, we're definitely just using l4[0], and l3[0], and l2 should be pretty easy:

    // Wait, can't I just edit my existing l2 table?  Turn on the u/s bit, and everthing with tables is kosher?

    // Herm, no, because l4 and l3 need u/s bit set too, right?  But I should be able to keep the same l2?

    l4[0] = (uint64_t) l3 | PT_PRESENT | PT_WRITABLE | PT_USERMODE;
    l3[0] = (uint64_t) l2 | PT_PRESENT | PT_WRITABLE | PT_USERMODE;
    // Identity map first GB without u/s, then mark just the one page with that bit
    // for (uint64_t i = 0; i < 512; i++)
    //     l2[i] = i * 2 * 1024 * 1024 | PT_PRESENT | PT_WRITABLE | PT_HUGE;
    //l2[(uint64_t) u / (1024 * 1024 * 2)] = (uint64_t) u | PT_PRESENT | PT_WRITABLE | PT_USERMODE | PT_HUGE;
    l2[(uint64_t) u / (1024 * 1024 * 2)] |= PT_USERMODE;
    com1_printf("u: 0x%h\n", u);
    com1_printf("@: 0x%h\n", (uint64_t) u / (1024 * 1024 * 2));

    // Something like this?:

    // push eflags // has iterrupt bit on
    // Disable interrupts
    // pop eflags  // has interrupt bit on
    ////// put that at top of user mode stack (consider alignment)
    // set ss to usermode gdt segment
    // set esp to user stack (consider alignment)
    // push or modified flags to now-usermode stack
    // put l4 in cr3
    // Set ds, es, fs, and gs to usermode gdt segment
    // iret

    //uint64_t* stack_top = (uint64_t*)(((uint64_t) stack + 1024 * 8) & ~0xf);

    __asm__ __volatile__
    (
     //"pushf\n"
         "cli\n"
         "pushf\n"
         "pop %%r8\n"
         "mov $16, %%ax\n"
         //"mov %%ax, %%ss\n" // trap!
         "mov %0, %%esp\n"
         //"push %%r8\n"
         "push $16\n"
         "mov %2, %%rax\n"
         "push %%rax\n"
         "push %%r8\n"
         "push $16\n"
         "mov %3, %%rax\n"
         "push %%rax\n"

         "mov %1, %%rax\n"

         //"jmp $0x10,$user\n"
         //"user:\n"

         //"hlt\n"
         "mov %%rax, %%cr3\n" // Hmm, maybe the moment I do this, I'm not able to access where I am anymore...  Here isn't paged...
         // That would mean I need to have the rest of memory mapped in this l2 as well, but without u/s bit.
         "iretq\n"
         ::"m"(stack_top), "m"(l4), "m"(kernel_stack_top), "m"(u)
    );
}

void __attribute__((section(".kernel_entry"))) kernel_entry() {
    uint32_t* entry_count = (uint32_t*) 0x4000;
    uint64_t largest = 0;
    uint32_t il;
    struct mem_table_entry* mem_table = (struct mem_table_entry*) 0x4004;
    for (uint32_t i = 0; i < *entry_count; i++) {
        if (mem_table[i].type != 1)
            continue;

        // L2 pages mapping first 256 GB of address space take up the MB from 0x100000 to 0x200000
        // At some point I'll probably want to make that part of this process, but for now it's fixed there,
        //   and we'll just account for it here.
        if (mem_table[i].start < 0x200000) {
            if (mem_table[i].start + mem_table[i].length < 0x200000) {
                mem_table[i].type = 2;
                continue;
            }

            mem_table[i].length -= 0x200000 - mem_table[i].start;
            mem_table[i].start = 0x200000;
        }

        if (mem_table[i].length > largest) {
            largest = mem_table[i].length;
            il = i;
        }
    }

    kernel_stack_top = (uint64_t*) ((mem_table[il].start + STACK_SIZE) & ~0b1111ull);
    init_heap(kernel_stack_top, mem_table[il].length - STACK_SIZE);

    init_interrupts();
    init_com1();
    startTty();

    logf("We've got %u mem table entries:\n", *entry_count);
    for (uint32_t i = 0; i < *entry_count; i++) {
        logf("0x%p016h - 0x%p016h : %u\n", mem_table[i].start,
               mem_table[i].start + mem_table[i].length - 1, mem_table[i].type);
    }
    logf("Largest memory region: 0x%p016h - 0x%p016h\n", mem_table[il].start, mem_table[il].start + largest - 1);
    logf("Stack top / heap bottom: 0x%h\n", kernel_stack_top);
    logf("Heap is %u MB.\n", heapSize() / 1024 / 1024);
    parse_acpi_tables();
    init_hpet();

    extern void* tss;
    *((void**) (tss + 4)) = kernel_stack_top;

    setUpUserMode();

    log("Kernel initialized; going to waitloop.\n");
    waitloop();
}
