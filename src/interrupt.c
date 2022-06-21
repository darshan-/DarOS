#include <stdarg.h>
#include <stdint.h>

#include "interrupt.h"

#include "io.h"
#include "keyboard.h"
#include "list.h"
#include "log.h"
#include "malloc.h"
#include "periodic_callback.h"
#include "periodic_callback_int.h"
#include "rtc_int.h"


// TODO: Keep thinking about what I want to do about sprinkling cli and sti all over the place.
//   It's iffy turning interrupts back on sometimes -- maybe they still need to be off because of
//     the caller.  Or maybe interrupts haven't been turned on yet at boot -- it feels wrong having
//     to be super aware of that.  Set up some kind of lock or semaphore system or something?
//     Pretty sure there's a better approach than what I'm doing so far.  (Mabye just an API, so
//     this file knows whether interrupts should be on yet, and can keep track of how many callers
//     have requested to disable interrupts.  If that number drops to zero and we otherwise want them
//     on, turn them on?


// TODO: Do I really want everything set up as INT rather than TRAP?  I like interrupts being turned off
//  for me automatically, but do I understand the implications of this change?  Am I really free to just
//  choose as a I please, whether a given interrupt vector is tagged as interrupt or trap?

/*

  If IRQ was initiated by the secondary PIC, that means both are involved, and both need an ack.
  If it was initiated by the primary, just send ack to that one.

 */

#define IDT 0
#define CODE_SEG 8 // 1 quadword past null descriptor; next segment would be 16, etc. TODO: Grab from CS?
#define TYPE_TRAP 0b1111
#define TYPE_INT 0b1110

#define PIC_ACK 0x20

#define ICW1 1<<4
#define ICW1_ICW4_NEEDED 1

#define PIC_PRIMARY_CMD 0x20
#define PIC_PRIMARY_DATA 0x21
#define PIC_SECONDARY_CMD 0xa0
#define PIC_SECONDARY_DATA 0xa1

#define TICK_HZ 1000

uint64_t int_tick_hz = TICK_HZ;  // For periodic_callbacks.c to access.

#define PIT_CMD 0x43
#define PIT_CH0_DATA 0x40
#define PIT_CH2_DATA 0x42
#define PIT_CHAN_0 0
#define PIT_CHAN_2 (1<<7)
#define PIT_WORD_RW (0b11<<4) // Read or write low byte then high byte, rather than just one byte
#define PIT_PERIODIC (0b10<<1)
//#define PIT_COUNT 65536
#define PIT_FREQ 1193182
#define PIT_COUNT (PIT_FREQ / TICK_HZ)

uint64_t* kernel_stack_top;

// Need extra level of indirection to quote a macro value, due to a special rule around argument prescan.
#define QUOT(v) #v
#define QUOTE(v) QUOT(v)

struct interrupt_frame {
    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t sp;
    uint64_t ss;
};


#define INIT_WQ_CAP 20
#define INIT_KB_CAP 20

uint64_t int_blocks = 0;

uint64_t read_tsc() {
    uint64_t tsc;

    // edx gets high-order doubleword, eax gets low-order doubleword

    __asm__ __volatile__(
        "cpuid\n"
        "rdtsc\n"
        "shl $32, %%rdx\n"
        "add %%rdx, %%rax\n"
        "mov %%rax, %0\n"
        :"=m"(tsc)
    );

    return tsc;
}

static uint64_t pitCount = 0;

struct queue {
    void** start;
    void** head;
    void** tail;
    uint64_t cap;
};

static struct queue wq = {};
// It's quite space inefficient to have 8-byte wide slots for key codes, but I don't expect the buffer to
//   ever get very full, and it's simplest to just have one queue type, I think?
static struct queue kbd_buf = {};


#define INITQ(q, c) q.start = q.head = q.tail = malloc(c * sizeof(void*)); \
    q.cap = c

#define INC_Q_PT(q, p) (q->p)++; if ((q->p) == q->start + q->cap) (q->p) = q->start;

static inline void* pop(struct queue* q) {
    __asm__ __volatile__("cli"); // I think I want to special case this and not use no_ints() and ints_okay()?

    if (q->head == q->tail) return 0;

    void* p = *(q->head);
    INC_Q_PT(q, head);

    __asm__ __volatile__("sti");

    return p;
}

// To be called only when interrupts are off (From handler or during initialization);
static inline void push(struct queue* q, void* p) {
    if (q->tail + 1 == q->head || (q->tail + 1 == q->start + q->cap && q->head == q->start)) {
        q->cap *= 2;
        uint64_t head_off = q->head - q->start;
        uint64_t tail_off = q->tail - q->start;
        q->start = realloc(q->start, q->cap * sizeof(void*));

        q->head = q->start + head_off;
        q->tail = q->start + tail_off;
    }

    *(q->tail) = p;
    INC_Q_PT(q, tail);
}

#define PT_PRESENT  1
#define PT_WRITABLE 1 << 1
#define PT_USERMODE 1 << 2
#define PT_HUGE     1 << 7

void userMode() {
    for(;;)
        //__asm__ __volatile__ ("mov $0x1234face, %r15\nint $0x80");
        //__asm__ __volatile__ ("inc %r15\ncli\n");
        __asm__ __volatile__ ("inc %r15\n");
}

void setUpUserMode() {
    uint8_t* u = malloc(2 * 1024 * 1024 * 2); // 2MB page, doubled so I can clunkily align
    // print("\n");
    // printf("u: 0x%h\n", u);
    u = (uint8_t*) ((uint64_t) (u + 1024 * 1024 * 2) & ~(1024 * 1024 * 2 - 1));
    void* stack_top = u + 2 * 1024 * 1024 - 1024;

    //__asm__ __volatile__ ("xchgw %bx, %bx");

    uint8_t* uc = (uint8_t*) &userMode;
    //__asm__ __volatile__ ("xchgw %bx, %bx");
    for (uint64_t i = 0; i < 512; i++)
        u[i] = uc[i];
    //__asm__ __volatile__ ("xchgw %bx, %bx");
    uint64_t* tables = mallocz(1024 * 4 * 2 * 2); // 2 4K tables, doubled so I can clunkily align
    uint64_t* l4 = (uint64_t*) ((uint64_t) (tables + 0x1000)  & ~0xfff);
    uint64_t* l3 = l4 + 0x1000 / 8;
    uint64_t* l2 = (uint64_t*) 0x100000;

    l4[0] = (uint64_t) l3 | PT_PRESENT | PT_WRITABLE | PT_USERMODE;
    l3[0] = (uint64_t) l2 | PT_PRESENT | PT_WRITABLE | PT_USERMODE;
    for (uint64_t i = 0; i < 512; i++) // TODO: only set user page, not full table
        l2[i] |= PT_USERMODE;
    // l2[(uint64_t) u / (1024 * 1024 * 2)] |= PT_USERMODE;

    __asm__ __volatile__
    (
        "mov %0, %%rax\n"
        "mov %%rax, %%cr3\n"
        ::"m"(l4)
    );

    //printf("u stack top: 0x%h\n", stack_top);
    __asm__ __volatile__ ("cli");
    print("About to user mode\n");
    __asm__ __volatile__
    (
        "cli\n"
        "mov %0, %%rax\n"
        "mov %%rax, %%rsp\n"
        "push $27\n"
        "push %%rax\n"
        "pushf\n"
        "pop %%rax\n"
        "or $0x200, %%rax\n"
        "mov %%rax, %%r13\n"
        "push %%rax\n"
        "push $19\n"
        "mov %1, %%rax\n"
        "push %%rax\n"
        "iretq\n"
        ::"m"(stack_top), "m"(u)
    );

    return;

    __asm__ __volatile__
    (
        //"pushf\n"
        "cli\n"
        //"pop %%r8\n"
        //"xchgw %%bx, %%bx\n"
        //"sti\n"
        //"hlt\n"
        "mov %0, %%esp\n"
        "push $27\n"
        "mov %0, %%rax\n"
        "push %%rax\n"
        //"push %%r8\n"
        "pushf\n"
        "pop %%rax\n"
        "or $0x200, %%rax\n"
        "push %%rax\n"
        "push $19\n"
        "mov %1, %%rax\n"
        "push %%rax\n"
        "iretq\n"
        ::"m"(stack_top), "m"(u)
    );
}


// Can waitloop be scheduler?
// Do any kernel work, then bounce between any user processes?  And if none of the above, hlt loop?
// So most interrupts can return, to whatever was interrupted, but timer interrupt, if the time slice is up, will
//  call scheduler instead (that thing that we're not returning to will be gotten back to when schedule says it's time)?
// Well, we'd need to keep track of state ourselves, if not iretq-ing back there.  But stack is messed up?  How do we get
//  state?  Why is stack weird?
// And weren't keyboard interrupts *not* happening before?  Wait, are they happening when in user land?  Yes?  What changed?
// Oh, that's right, if all interrupt does is queue work then go back to userland, we never make it to waitoop to do queued
//   work.  So I think that's all working as expected.
// So main issue is storing all registers -- we're not just starting a process, we're trying to get back to exactly the
//   state of everything when going back to that line of code.  So restoring will mean not just setting up stack, but when
//   we call iretq, every single register should be restored first.
// Hmm, don't I still need to know if I'm in the interrupt from user land vs kernel code?  I mean, if the above works, then
//  all interrupts except timer (pit / hpet -- I plan to use HPET where available relatively soon) will just iretq (return
//  in C).  But timer interrupt will want to pause a user process if it's time.  (Oh, other interrupts will pause user process
//  too, like when waiting for disk or network or something -- but in that case, they don't need to know how they got there.)
// So I need to check stack or something, to know.  Because there is no process for kernel stuff, and if kernel is in the middle
//   of something and we just went to waitloop and abandoned it, we'd never get back to it.  Well, and that makes it obvious
//   what I'd thought briefly above but hadn't fully sat with -- we need to know not just whether we're here from users space,
//   but exactly which process -- how else could we save the registers for it?
// I guess the stack frame GCC presents me with should be sufficient... the IP of that should show me which process I'm in
//  (unless I'm doing something in the kernel on behalf of that process -- which a proper OS would keep track of, but maybe,
//  as far as playing, experimenting, and learning here, for now at least, I can look at that) -- if the stack frame IP is
//  part of userland process, and it's time to task switch, save registers for that process, and do process switch; but if it's
//  not part of a ring 3 proess, iretq to let kernel finish what it's doing?  (Hmm, kernel could also be doing things that
//  wait -- I/O, etc. -- *and* doing them on bahalf of a user process -- so it's not just a matter of being lax with time
//  acounting, it's also a matter of it not necessarily making sense to return there.  Hmm, or is it?  Hmm... well, if I was
//  waiting for disk or network io or somethign, I guess I'd set up data structures for what to do when the data is ready, and
//  then go to the waitloop.  So the basic design is that the kernel never does anything that takes a long time, and I should
//  always return to what the kernel was doing?  Ooh -- wait, what about waitloop?  I *don't* want to return to the hlt -- yeah
//  okay, but it's not a hlt loop, we always, for quite some time now, have been doing work at the top of that very loop, exactly
//  as we'd want.  Hmm, yeah, I gues I'm spacey and want to consider further when less spacy, but I think I'm good to iretq to
//  whatever ring 0 thing we were doing if GCC's interrupt stack frame IP isn't part of a user process.

// Hmm, in thinking about paging, and how IP wouldn't be reliable if things weren't identity mapped, and wondering how this is
//   supposed to work, it finally just occured to me that -- duh! -- the kernel can know what it was ding by keeping track
//   itself, right?  When about to switch to a user process, we mark that as the active process (after turning interrupts off,
//   obviously, so we're not going to get an interrupt between setting that and actually *being* in that process).  So we know
//   whether to iretq back to kernel or to go to waitlist/scheduler based on that.  If it's time to schedule, and there's an
//   active ring 3 process, go to scheduler.  Otherwise iretq.

void waitloop() {
    for (;;) {
        void (*f)();

        while ((f = (void (*)()) pop(&wq)))
            f();

        __asm__ __volatile__(
            "mov %0, %%rsp\n" // We'll never return anywhere or use anything currently on the stack, so reset it
            "sti\n"
            "hlt\n"
            ::"m"(kernel_stack_top)
        );
    }
}

void process_keys() {
    uint8_t code = (uint8_t) (uint64_t) pop(&kbd_buf);
    keyScanned(code);
}

static void dumpFrame(struct interrupt_frame *frame) {
    logf("ip: 0x%p016h    cs: 0x%p016h flags: 0x%p016h\n", frame->ip, frame->cs, frame->flags);
    logf("sp: 0x%p016h    ss: 0x%p016h\n", frame->sp, frame->ss);
}

static inline void generic_trap_n(struct interrupt_frame *frame, int n) {
    logf("Generic trap handler used for trap vector 0x%h\n", n);
    dumpFrame(frame);

    // In generic case, it's not safe to do anything but go to waitloop (well, that may well not be safe either;
    //   halting the machine completly is probably best, but for now I'd like to do it this way rather than
    //   that or risk jumping to IP.
    // That means we can ignore whether there is an error code on the stack, as waitloop clears stack anyway.
    // So I think this should be a fine generic trap handler to default to when a specific one isn't available.
    frame->ip = (uint64_t) waitloop;
}

static inline void generic_etrap_n(struct interrupt_frame *frame, uint64_t error_code, int n) {
    logf("Generic trap handler used for trap vector 0x%h, with error on stack; error: 0x%p016h\n", n, error_code);
    dumpFrame(frame);
    frame->ip = (uint64_t) waitloop;
}


// Is there an easier/cleaner/more efficient way to do this?  Macros are weird, clunky, too powerful and too
//   limited at the same time...

#define SET_GTRAP_N(nn) set_handler(0x##nn, trap_handler_0x##nn, TYPE_INT);

#define TRAP_N(nn) static void __attribute__((interrupt)) trap_handler_0x##nn(struct interrupt_frame *frame) {\
    generic_trap_n(frame, 0x##nn);\
}

#define SET_GETRAP_N(nn) set_handler(0x##nn, etrap_handler_0x##nn, TYPE_INT);

#define ETRAP_N(nn) static void __attribute__((interrupt)) etrap_handler_0x##nn(struct interrupt_frame *frame, uint64_t ec) { \
    generic_etrap_n(frame, ec, 0x##nn);\
}

#define TRAP_HL(macro, h, l) macro(h##l)

#define TRAPS(macro, h) \
TRAP_HL(macro, h, 0)\
TRAP_HL(macro, h, 1)\
TRAP_HL(macro, h, 2)\
TRAP_HL(macro, h, 3)\
TRAP_HL(macro, h, 4)\
TRAP_HL(macro, h, 5)\
TRAP_HL(macro, h, 6)\
TRAP_HL(macro, h, 7)\
TRAP_HL(macro, h, 8)\
TRAP_HL(macro, h, 9)\
TRAP_HL(macro, h, a)\
TRAP_HL(macro, h, b)\
TRAP_HL(macro, h, c)\
TRAP_HL(macro, h, d)\
TRAP_HL(macro, h, e)\
TRAP_HL(macro, h, f)

TRAPS(TRAP_N, 0)
TRAPS(TRAP_N, 1)

//ETRAP_N(08)
ETRAP_N(0a)
ETRAP_N(0b)
ETRAP_N(0c)
ETRAP_N(0d)
//ETRAP_N(0e)
ETRAP_N(11)
ETRAP_N(15)
ETRAP_N(1d)
ETRAP_N(1e)


static void init_pic() {
    // ICW1
    outb(PIC_PRIMARY_CMD, ICW1 | ICW1_ICW4_NEEDED);
    outb(PIC_SECONDARY_CMD, ICW1 | ICW1_ICW4_NEEDED);

    // ICW2
    outb(PIC_PRIMARY_DATA, 0x20);   // Map  primary  PIC to 0x20 - 0x27
    outb(PIC_SECONDARY_DATA, 0x28); // Map secondary PIC to 0x28 - 0x2f

    // ICW3
    outb(PIC_PRIMARY_DATA, 1<<2);   // Secondary is at IRQ 2
    outb(PIC_SECONDARY_DATA, 2);    // Secondary is at IRQ 2

    // ICW4
    outb(PIC_PRIMARY_DATA, 1);      // 8086/8088 mode
    outb(PIC_SECONDARY_DATA, 1);    // 8086/8088 mode

    // Mask interrupts as you see fit
    //outb(PIC_PRIMARY_DATA, 0x7c);
    outb(PIC_PRIMARY_DATA, 0xfc);
    outb(PIC_SECONDARY_DATA, 0xff);
}

// void unmask_pics() {
//     outb(PIC_PRIMARY_DATA, 0);
//     outb(PIC_SECONDARY_DATA, 0);
// }

static void __attribute__((interrupt)) default_interrupt_handler(struct interrupt_frame *frame) {
    logf("Default interrupt handler\n");
    //dumpFrame(frame);
}

static void __attribute__((interrupt)) int_0x80_handler(struct interrupt_frame *frame) {
    print("int 0x80 handler\n");

    dumpFrame(frame);
    //frame->ip = (uint64_t) waitloop;
    //print("waitloop?");
    //printf("hlt_in_waitloop in int 0x80: %u\n", hlt_in_waitloop);
    __asm__ __volatile__
    (
        "mov %0, %%rax\n"
        "mov %%rax, %%rsp\n"
        "push $0\n"
        "push %%rax\n"
        "pushf\n"
        "pop %%rax\n"
        "or $0x200, %%rax\n"
        "mov %%rax, %%r13\n"
        "push %%rax\n"
        "push $8\n"
        "mov %1, %%rax\n"
        "push %%rax\n"
        "iretq\n"
        ::"m"(kernel_stack_top), "m"(waitloop)
    );


    //for (int i = 0; i < 5; i++) {
    int i = 0;
        uint64_t v;
        __asm__ __volatile__ ("pop %%rax\n mov %%rax, %0\n":"=m"(v)::"%rax");
        printf("%u: 0x%p016h\n", i, v);
        i = 1;
        __asm__ __volatile__ ("pop %%rax\n mov %%rax, %0\n":"=m"(v)::"%rax");
        printf("%u: 0x%p016h\n", i, v);
        i = 2;
        __asm__ __volatile__ ("pop %%rax\n mov %%rax, %0\n":"=m"(v)::"%rax");
        printf("%u: 0x%p016h\n", i, v);
        i = 3;
        __asm__ __volatile__ ("pop %%rax\n mov %%rax, %0\n":"=m"(v)::"%rax");
        printf("%u: 0x%p016h\n", i, v);
        i = 4;
        __asm__ __volatile__ ("pop %%rax\n mov %%rax, %0\n":"=m"(v)::"%rax");
        printf("%u: 0x%p016h\n", i, v);
        i = 5;
        __asm__ __volatile__ ("pop %%rax\n mov %%rax, %0\n":"=m"(v)::"%rax");
        printf("%u: 0x%p016h\n", i, v);
        i = 6;
        __asm__ __volatile__ ("pop %%rax\n mov %%rax, %0\n":"=m"(v)::"%rax");
        printf("%u: 0x%p016h\n", i, v);
        //}
    // Let's look at stack and see what's there, because that's what determines what happens when we iretq at end of handler...
    __asm__ __volatile__ ("hlt");
}

static void __attribute__((interrupt)) default_PIC_P_handler(struct interrupt_frame *frame) {
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    logf("Default primary PIC interrupt handler\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) default_PIC_S_handler(struct interrupt_frame *frame) {
    outb(PIC_SECONDARY_CMD, PIC_ACK);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    logf("Default secondary PIC interrupt handler\n");
    dumpFrame(frame);
}

static void __attribute__((interrupt)) divide_by_zero_handler(struct interrupt_frame *frame) {
    logf("Divide by zero handler\n");
    frame->ip = (uint64_t) waitloop;
    dumpFrame(frame);
}

static void __attribute__((interrupt)) trap_0x0e_page_fault(struct interrupt_frame *frame, uint64_t error_code) {
    logf("Page fault; error: 0x%p016h\n", error_code);
    dumpFrame(frame);

    uint64_t cr2;

    __asm__ __volatile__(
        "mov %%cr2, %%rax\n"
        "mov %%rax, %0\n"
        :"=m"(cr2)
    );

    logf("cr2: %p016h\n", cr2);

    frame->ip = (uint64_t) waitloop;
}

static void __attribute__((interrupt)) double_fault_handler(struct interrupt_frame *frame, uint64_t error_code) {

    logf("Double fault; error should be zero.  error: 0x%p016h\n", error_code);
    dumpFrame(frame);
}

static void __attribute__((interrupt)) irq1_kbd(struct interrupt_frame *) {
    uint8_t code = inb(0x60);
    outb(PIC_PRIMARY_CMD, PIC_ACK);
    push(&kbd_buf, (void*) (uint64_t) code);
    push(&wq, process_keys);

    // I may have been doing something in ring 0 that I want to return to; this is just an experiment
    // __asm__ __volatile__
    // (
    //     "mov %0, %%rax\n"
    //     "mov %%rax, %%rsp\n"
    //     "push $0\n"
    //     "push %%rax\n"
    //     "pushf\n"
    //     "pop %%rax\n"
    //     "or $0x200, %%rax\n"
    //     "mov %%rax, %%r13\n"
    //     "push %%rax\n"
    //     "push $8\n"
    //     "mov %1, %%rax\n"
    //     "push %%rax\n"
    //     "iretq\n"
    //     ::"m"(kernel_stack_top), "m"(waitloop)
    // );
}

static void __attribute__((interrupt)) irq8_rtc(struct interrupt_frame *) {
    outb(PIC_SECONDARY_CMD, PIC_ACK);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    // uint8_t type = irq8_type();
    // if (type == RTC_INT_PERIODIC) {
    // }
}

static uint64_t cpuCountOffset = 0;

static void __attribute__((interrupt)) irq0_pit(struct interrupt_frame *) {
    //logf("0");
    outb(PIC_PRIMARY_CMD, PIC_ACK);
    //print("irq0_pit\n");

    //print("acked irq0\n");
    //__asm__ __volatile__("hlt");
    // if (cpuCountOffset == 0)
    //     cpuCountOffset = read_tsc();

    pitCount++;

    ms_since_boot = pitCount * PIT_COUNT * 1000 / PIT_FREQ;
    static lpc = 0;
    if (ms_since_boot % 1000 == 0 && pitCount != lpc) {
        lpc = pitCount;
        //print("irq0\n");
        printf("pitCount: %u\n", pitCount);
    }

    // if (pitCount % TICK_HZ == 0)
    //     logf("Average CPU ticks per PIT tick: %u\n", (read_tsc() - cpuCountOffset) / pitCount);

    if (!periodicCallbacks.pcs) return;

    for (uint64_t i = 0; i < periodicCallbacks.len; i++) {
        if (periodicCallbacks.pcs[i]->count == 0 || periodicCallbacks.pcs[i]->count > TICK_HZ) {
            logf("halting!\n");
            __asm__ __volatile__ ("hlt");
        }

        if (pitCount % (TICK_HZ * periodicCallbacks.pcs[i]->period / periodicCallbacks.pcs[i]->count) == 0)
            push(&wq, periodicCallbacks.pcs[i]->f);
    }

    if (ms_since_boot > 100)
        waitloop();
}

static void set_handler(uint64_t vec, void* handler, uint8_t type) {
    uint16_t* idt_entry = (uint16_t*) (IDT + 16 * vec);
    uint64_t offset = (uint64_t) handler;
    *idt_entry = (uint16_t) (offset & 0xffff);
    offset >>= 16;
    *(idt_entry + 3) = (uint16_t) (offset & 0xffff);
    offset >>= 16;
    *((uint32_t*) (idt_entry + 4)) = (uint32_t) offset;

    *(idt_entry + 2) = (uint16_t) 1 << 15 | type << 8 | 3 << 13; // TODO: Only set ring-3 callable for syscall interrupt
    *(idt_entry + 1) = CODE_SEG;
    *((uint32_t*) (idt_entry + 6)) = 0;
}

static void init_pit() {
    outb(PIT_CMD, PIT_CHAN_0 | PIT_WORD_RW | PIT_PERIODIC);
    outb(PIT_CH0_DATA, PIT_COUNT & 0xff);
    outb(PIT_CH0_DATA, PIT_COUNT >> 8);
}

// TODO: Grow queue capacities if appropriate
    // TODO:
    // I can have a work queue item that runs periodically (say, every 10 seconds?) that checks capacity
    //   and usage, and doubles capacity if usage is greater than half of capacity.

static void check_queue_caps() {
    //logf("kbd_buf now has capacity: %u\n", kbd_buf.cap);
}

void init_interrupts() {
    no_ints();
    for (int i = 32; i < 40; i++)
        set_handler(i, default_PIC_P_handler, TYPE_INT);
    for (int i = 40; i < 48; i++)
        set_handler(i, default_PIC_S_handler, TYPE_INT);
    for (int i = 48; i < 256; i++)
        set_handler(i, default_interrupt_handler, TYPE_INT);

    set_handler(0x20, irq0_pit, TYPE_INT);
    set_handler(0x21, irq1_kbd, TYPE_INT);
    set_handler(0x28, irq8_rtc, TYPE_INT);

    TRAPS(SET_GTRAP_N, 0);
    TRAPS(SET_GTRAP_N, 1);

    set_handler(0, divide_by_zero_handler, TYPE_INT);

    // These are the traps with errors on stack according to https://wiki.osdev.org/Exceptions, 
    set_handler(8, double_fault_handler, TYPE_INT);
    //SET_GETRAP_N(08);
    SET_GETRAP_N(0a);
    SET_GETRAP_N(0b);
    SET_GETRAP_N(0c);
    SET_GETRAP_N(0d);
    //SET_GETRAP_N(0e);
    set_handler(0x0e, trap_0x0e_page_fault, TYPE_INT);
    SET_GETRAP_N(11);
    SET_GETRAP_N(15);
    SET_GETRAP_N(1d);
    SET_GETRAP_N(1e);

    set_handler(0x80, int_0x80_handler, TYPE_INT);

    init_rtc();
    init_pit();
    init_pic();
    set_handler(0x20, irq0_pit, TYPE_INT);
    set_handler(0x21, irq1_kbd, TYPE_INT);
    INITQ(wq, INIT_WQ_CAP);
    INITQ(kbd_buf, INIT_KB_CAP);

    cpuCountOffset = read_tsc();

    registerPeriodicCallback((struct periodic_callback) {1, 2, check_queue_caps});

    //__asm__ __volatile__ ("xchgw %bx, %bx");
    ints_okay();
}
