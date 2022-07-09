#include <stdarg.h>
#include <stdint.h>

#include "interrupt.h"

#include "console.h"
#include "io.h"
#include "keyboard.h"
#include "list.h"
#include "log.h"
#include "periodic_callback.h"
#include "periodic_callback_int.h"
#include "rtc_int.h"

#include "../lib/malloc.h"
#include "../lib/strings.h"

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

// 4=REX
//  9 = 0b1001 WRXB
//             W = quadword operand
//               X is index field extension; not sued here
//              R and B combine with below
//    ff = inc
//       c7 = 0b_11000111 (combines with above)
//               11 = ModRM
//                 000111
//                 000_111
//                R000_B111
//                0000_1111 = 15 = r15
// 49 ff c7   inc r15
// eb fb      jmp -5      eb = jmp, fb = -5

struct process {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t rip;
    uint64_t rsp;
    uint64_t rflags;

    uint64_t stdout;

    void* page; // For now only one page allowed

    struct process* waiting; // For now just one process can wait for a given process to exit
};

// Huh, what if I didn't keep a list of waiting/sleeping procs?  Terminal has a reference, and can send termination signal, or readline, etc.
// Presumably it will be natural for other sleep reasons (like sleeping for a given time, or reading from disk or network, if those are ever
//   things here) for them to also have a reference to their relevant process, or else I'd have a list special to whatever it was.
// So maybe runnableProcs is only list here?
// We'll want to make sure in that case to not assume every process has a node...
// I mean, ultimately, it feels pretty wrong to not be able to say, here, here's all the processes.
// Hmm, at some point, maybe soon, perhaps each process will have a list of subprocesses?  So you can hold "init" process, and walk the
//   tree to all other processes?
static struct list* runnableProcs = (struct list*) 0;
//static struct list* readyProcs = (struct list*) 0;
//static struct list* sleepingProcs = (struct list*) 0;

static struct process* curProc = 0;
void* curProcN = 0;

void killProc(struct process* p) {
    if (!p)
        return;

    free(p->page);
    //procDone(p->at);  Hmm, not really... might be sub-processes...
    // I guess I need to keep track of top-level process that is keeping shell from prompting?
    // Or give shell a process ID, and don't tell shell "proc done on terminal t", but do tell it "process with id x is done"?
    // I think the latter?  (Well, the latter is so console does the former, I guess?)
    // That way you can have a background process, by not waiting for it -- and you can have something similar if a process launched
    //   another process, and the first one returned at some point after that.  That first one returning is the signal the console needs
    //   to go ahead and prompt again.  The sub-process can still write to the termainal, and is a background process.
    procDone(p, p->stdout); // Would rather console doesn't understand proc; it just sees p as a unique id.

    // if (p->waiting) // Enqueue work of returning to wait() call of waiting process
    //     pushListHead(readyProcs, p->waiting);
    if (p->waiting)
        pushListTail(runnableProcs, p->waiting);

    free(p);

    if (!curProcN)
        return;

    if (p == curProc) {
        void* n = nextNodeCirc(runnableProcs, curProcN);
        int same = n == curProcN;
        // if (p->sleeping)
        //     removeNodeFromList(sleepingProcs, curProcN);
        // else
        removeNodeFromList(runnableProcs, curProcN);

        if (same) {
            curProcN = 0;
            curProc = 0;
        } else {
            curProcN = n;
            curProc = listItem(n);
        }
    } else {
        removeFromList(runnableProcs, p);
    }
}

// Caller should probably call no_ints before calling, and wait until after it's used the process's memory to call ints_okay, I think?
void mapProcMem(struct process* p) {
    uint64_t* l2 = (uint64_t*) (0x100000 + (511 * 4096));
    l2[0] = (uint64_t) p->page | PT_PRESENT | PT_WRITABLE | PT_HUGE | PT_USERMODE;

    asm volatile ("\
\n      mov $0x7FC0000000, %rax                \
\n      invlpg (%rax)                          \
    ");
}

void startProc(struct process* p) {
    asm volatile ("cli");

    mapProcMem(p);

    uint64_t* sp = (uint64_t*) p->rsp;
    *--sp = 27;
    *--sp = p->rsp;
    *--sp = p->rflags | 0x200;
    *--sp = 19;
    *--sp = p->rip;

    *--sp = p->rax;
    *--sp = p->rbx;
    *--sp = p->rcx;
    *--sp = p->rdx;
    *--sp = p->rsi;
    *--sp = p->rdi;
    *--sp = p->rbp;
    *--sp = p->r8;
    *--sp = p->r9;
    *--sp = p->r10;
    *--sp = p->r11;
    *--sp = p->r12;
    *--sp = p->r13;
    *--sp = p->r14;
    *--sp = p->r15;

    asm volatile ("mov %0, %%rsp"::"m"(sp));

    asm volatile ("\
\n      pop %r15                                \
\n      pop %r14                                \
\n      pop %r13                                \
\n      pop %r12                                \
\n      pop %r11                                \
\n      pop %r10                                \
\n      pop %r9                                 \
\n      pop %r8                                 \
\n      pop %rbp                                \
\n      pop %rdi                                \
\n      pop %rsi                                \
\n      pop %rdx                                \
\n      pop %rcx                                \
\n      pop %rbx                                \
\n      pop %rax                                \
\n      iretq                                   \
    ");
}

struct app {
    uint64_t* code;
    uint64_t len;
};

extern uint64_t app_code[];
extern uint64_t app_code_len;
static struct app app;

extern uint64_t sh_code[];
extern uint64_t sh_code_len;
static struct app sh;

static void* startApp(struct app* a, uint64_t stdout) {
    struct process *p = mallocz(sizeof(struct process));
    p->page = palloc();
    p->stdout = stdout;
    for (uint64_t i = 0; i < a->len; i++)
        ((uint64_t*) (p->page))[i] = a->code[i];

    p->r15 = stdout;
    p->rip = 0x7FC0000000ull;
    p->rsp = 0x7FC0180000ull;
    asm volatile ("\
\n      pushf                                       \
\n      pop %%rax                                   \
\n      mov %%rax, %0                               \
    " : "=m"(p->rflags));

    pushListTail(runnableProcs, p); // TODO: I may have assumptions elsewhere that aren't met with this as is...

    return p;
}

void* startSh(uint64_t stdout) {
    return startApp(&sh, stdout);
}

void gotLine(void* v, char* l) {
    struct process* p = v;
    p->rax = strlen(l);
    p->rbx = p->rsp - p->rax - (20 * 8); // We push 20 quadwords onto stack as part of resuming process

    char* s = (char*) (p->page + p->rbx - 0x7FC0000000ull);
    for (uint64_t i = 0; i < p->rax; i++)
        s[i] = l[i];

    pushListTail(runnableProcs, p);
}

// TODO: Known issues:
// In qemu, sometimes freezing after launching app (doesn't seem to happen in bochs...)

// And that's back, just before I made it here to delete that.
// I've determined that it appears *not* to be about missing turning interrupts back on.  Certainly ints_okay() appears to be called more
//   recently than no_ints().  (I'm showing in status bar to assess this.)
// *And* I noticed my CPU fan getting loud a few seconds after freezing, and HTOP shows qemu CPU usage at 100%.
// So I think I have an infinite loop somewhere.  Likely, I think, something a bit subtle, that is ultimately an infinite loop.
// If I could replicate in bochs, that would make it easier to track down.  Although it's occuring to me now that qemu I think still does
//   allow me to inspect registers (and memory, but I think rip is fairly likely to be my main clue as to what's going on).
// Otherwise maybe write serial output to a file on the host system (or, I guess I can still see that too -- yeah; I'm just used to my log
//   console now, and blurring that with the serial console.  So, if I can't sort it out otherwise, having logs go to serial console and
//   looking there is probably my best approach to figuring out what's going on.

void waitloop() {
    for (;;) {
        void (*f)();

        while ((f = (void (*)()) pop(&wq)))
            f();

        asm volatile("cli");

        if (curProcN)
            curProcN = nextNodeCirc(runnableProcs, curProcN);
        else if (listLen(runnableProcs)) // TODO: I feel like there's a cleaner approach to sort out when I'm less tired...
            curProcN = listHead(runnableProcs);

        if (curProcN) {
            curProc = listItem(curProcN);
            startProc(curProc);
        }

        asm volatile (
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
    dumpFrame(frame);
}

extern uint64_t regs[15];

void __attribute__((interrupt)) int0x80_syscall(struct interrupt_frame *frame) {
    if (frame->ip >= 511ull * 1024 * 1024 * 1024) {
        for (int i = 0; i < 15; i++)
            ((uint64_t*) curProc)[i] = regs[i];
        curProc->rip = frame->ip;
        curProc->rsp = frame->sp;
        curProc->rflags = frame->flags;

        switch (curProc->rax) {
        case 0: // exit()
            killProc(curProc);
            waitloop();
            break;
        case 2: // printColor(char* s, color c)
            no_ints(); // Printing will disable and then reenable, but we want them to stay off until iretq, so inc count of noes
            printColorTo(curProc->stdout, (char*) curProc->rbx, (uint8_t) curProc->rcx); // This is a safe way to get just low 8-bits, right?
            ints_okay_once_on(); // dec count of noes, so count is restored and iretq turns them on

            break;
        case 3: // readline()
            setReading(curProc->stdout, curProc);
            removeNodeFromList(runnableProcs, curProcN);
            waitloop();
            break;
        case 4: // runProg(char* s)
            if (!strcmp((char*) curProc->rbx, "app"))
                curProc->rax = (uint64_t) startApp(&app, curProc->stdout);
            else if (!strcmp((char*) curProc->rbx, "sh"))
                curProc->rax = (uint64_t) startApp(&sh, curProc->stdout);
            else
                curProc->rax = 0;

            startProc(curProc);
            break;
        case 5: // wait(uint64_t p)
            ((struct process*) curProc->rbx)->waiting = curProc;
            removeNodeFromList(runnableProcs, curProcN);
            waitloop();
            break;
        default:
            logf("Unknown syscall 0x%h\n", curProc->rax);
        }
    }
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
    printf("\nPage fault; error: 0x%p016h\n", error_code);
    logf("Page fault; error: 0x%p016h\n", error_code);
    dumpFrame(frame);

    uint64_t cr2;

    __asm__ __volatile__(
        "mov %%cr2, %%rax\n"
        "mov %%rax, %0\n"
        :"=m"(cr2)
    );

    logf("cr2: %p016h\n", cr2);

    if (frame->cs == 0x13)
        killProc(curProc);

    waitloop();
    //frame->ip = (uint64_t) waitloop;
    // TODO: Just call waitloop?
}

static void __attribute__((interrupt)) double_fault_handler(struct interrupt_frame *frame, uint64_t error_code) {
    logf("Double fault; error should be zero.  error: 0x%p016h\n", error_code);
    dumpFrame(frame);
}

static void __attribute__((interrupt)) irq1_kbd(struct interrupt_frame *) {
    uint8_t code = inb(0x60);
    outb(PIC_PRIMARY_CMD, PIC_ACK);
    push(&kbd_buf, (void*) (uint64_t) code);
    //printf("[%u]", code);
    push(&wq, process_keys);
}

static void __attribute__((interrupt)) irq8_rtc(struct interrupt_frame *) {
    outb(PIC_SECONDARY_CMD, PIC_ACK);
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    // uint8_t type = irq8_type();
    // if (type == RTC_INT_PERIODIC) {
    // }
}

static uint64_t cpuCountOffset = 0;

void __attribute__((interrupt)) irq0_pit(struct interrupt_frame *frame) {
    outb(PIC_PRIMARY_CMD, PIC_ACK);

    pitCount++;

    ms_since_boot = pitCount * PIT_COUNT * 1000 / PIT_FREQ;

    if (periodicCallbacks.pcs) {
        for (uint64_t i = 0; i < periodicCallbacks.len; i++) {
            if (periodicCallbacks.pcs[i]->count == 0 || periodicCallbacks.pcs[i]->count > TICK_HZ) {
                printf("halting!\n");
                logf("halting!\n");
                __asm__ __volatile__ ("hlt");
            }

            if (pitCount % (TICK_HZ * periodicCallbacks.pcs[i]->period / periodicCallbacks.pcs[i]->count) == 0)
                push(&wq, periodicCallbacks.pcs[i]->f);
        }
    }

    static uint64_t lms = 0;
    if (ms_since_boot >= lms + 2) {
        lms = ms_since_boot;

        if (frame->ip >= 511ull * 1024 * 1024 * 1024) {
            for (int i = 0; i < 15; i++)
                ((uint64_t*) curProc)[i] = regs[i];
            curProc->rip = frame->ip;
            curProc->rsp = frame->sp;
            curProc->rflags = frame->flags;

            waitloop();
        }
    }
}

static void set_handler(uint64_t vec, void* handler, uint8_t type) {
    uint16_t* idt_entry = (uint16_t*) (IDT + 16 * vec);
    uint64_t offset = (uint64_t) handler;
    *idt_entry = (uint16_t) (offset & 0xffff);
    offset >>= 16;
    *(idt_entry + 3) = (uint16_t) (offset & 0xffff);
    offset >>= 16;
    *((uint32_t*) (idt_entry + 4)) = (uint32_t) offset;

    uint16_t ring = vec == 0x80 ? 3 << 13 : 0;
    *(idt_entry + 2) = (uint16_t) 1 << 15 | type << 8 | ring;
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

    //set_handler(0x20, irq0_pit, TYPE_INT);
    extern void irq0();
    printf("&irq0: 0x%h\n", irq0);
    //asm volatile("cli\nhlt");
    set_handler(0x20, &irq0, TYPE_INT);
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

    //set_handler(0x80, int_0x80_handler, TYPE_INT);
    extern void int0x80();
    set_handler(0x80, int0x80, TYPE_INT);
    //set_handler(0x80, int0x80_syscall, TYPE_INT);

    init_rtc();
    init_pit();
    init_pic();
    //set_handler(0x20, irq0_pit, TYPE_INT);
    //set_handler(0x21, irq1_kbd, TYPE_INT);
    INITQ(wq, INIT_WQ_CAP);
    INITQ(kbd_buf, INIT_KB_CAP);

    cpuCountOffset = read_tsc();

    registerPeriodicCallback((struct periodic_callback) {1, 2, check_queue_caps});

    runnableProcs = newList();
    //readyProcs = newList();
    //sleepingProcs = newList();
    //curProc = malloc(sizeof(struct process));

    //__asm__ __volatile__ ("xchgw %bx, %bx");

    app.code = &app_code[0];
    app.len = app_code_len;

    sh.code = &sh_code[0];
    sh.len = sh_code_len;

    ints_okay();
}
