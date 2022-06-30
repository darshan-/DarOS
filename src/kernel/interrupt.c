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
    void* page; // For now only one page allowed

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

    uint64_t stdout;

    uint64_t wait;
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

void startProc(struct process* p) {
    asm volatile ("cli");

    uint64_t* l2 = (uint64_t*) (0x100000 + (511 * 4096));
    l2[0] = (uint64_t) p->page | PT_PRESENT | PT_WRITABLE | PT_HUGE | PT_USERMODE;

    asm volatile ("\
\n      mov $0x7FC0000000, %rax                \
\n      invlpg (%rax)                          \
    ");

    uint64_t flags;
    asm volatile ("\
\n      pushf                                   \
\n      pop %%rax                               \
\n      or $0x200, %%rax                        \
\n      mov %%rax, %0                           \
    " : "=m"(flags));

    uint64_t* sp = (uint64_t*) p->rsp;
    *--sp = 27;
    *--sp = p->rsp;
    *--sp = flags;
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

extern uint64_t app[];
extern uint64_t app_len;

//uint64_t wth[450] = {};// {0, 1};//, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9, 20};
uint64_t wth[500] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9,
                     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 4, 5, 6, 7, 8, 9
};

uint64_t app2[] = {
0x80000bee5894855ull,
0x7fc0180000b84800ull,
0xb848c78948000000ull,
0x7fc000030bull,
0x4800000000b8d0ffull,
0x7fc000009fbaull,
0xb8d2ff00ull,
0x7fc00001feba48ull,
0x48c35d90d2ff0000ull,
0xdd0b84810ec83ull,
0x8b480000007fc0ull,
0xd8b8480824448948ull,
0x480000007fc0000dull,
0xc0000dd0ba48008bull,
0x480289480000007full,
0x7fc0000dd0b8ull,
0x24448b48108b4800ull,
0xdd8b848c2014808ull,
0x89480000007fc000ull,
0x55c310c483489010ull,
0x4810ec8348e58948ull,
0x7fc0000dd0b8ull,
0x100c74800ull,
0x7fc0000dd8b848ull,
0x100c7480000ull,
0xb811eb00ull,
0x7fc0000047ba48ull,
0xdd8b848d2ff0000ull,
0x8b480000007fc000ull,
0x489e7ffffba4800ull,
0xd373c239488ac723ull,
0x7fc0000dd8b848ull,
0xd0b848108b480000ull,
0x480000007fc0000dull,
0x90b848c68948008bull,
0x480000007fc0000dull,
0x4800000000b8c789ull,
0x7fc0000209b9ull,
0x40f845c748d1ff00ull,
0xdd0b848077359ull,
0xc7480000007fc0ull,
0xb8486aeb00000000ull,
0x7fc0000dd0ull,
0xba008b48ull,
0x48d08948f875f748ull,
0xdd0b8482e75c085ull,
0x8b480000007fc000ull,
0xdc8b848c6894800ull,
0x89480000007fc000ull,
0xba4800000000b8c7ull,
0x7fc0000209ull,
0xc0000dd0b848d2ffull,
0x48008b480000007full,
0xdd0b84801508dull,
0x1089480000007fc0ull,
0x48d08948f8558b48ull,
0x148d0014802e0c1ull,
0xdd0b848c28948c0ull,
0x8b480000007fc000ull,
0xff6c820fd0394800ull,
0xb89066db8766ffffull,
0x1feba4800000000ull,
0xd2ff0000007fc000ull,
0xc748c3c990db8766ull,
0x9080cd00000000c0ull,
0x3c894858ec8348c3ull,
0x8948302474894824ull,
0x40244c8948382454ull,
0x4c894c482444894cull,
0x481024448d485024ull,
0x102444c708244489ull,
0x24448d4800000008ull,
0x8d48182444894860ull,
0x2024448948282444ull,
0x4800000001c0c748ull,
0x8244c8b48241c8bull,
0xc358c483489080cdull,
0x247c894810ec8348ull,
0x4804244488f08908ull,
0x8b4800000002c0c7ull,
0xcd04244c8a08245cull,
0x55c310c483489080ull,
0x4820ec8348e58948ull,
0x80cd00000003c0c7ull,
0xe05d8948e8458948ull,
0x1c08348e8458b48ull,
0x472b848c78948ull,
0x48d0ff0000007fc0ull,
0xf845c748f04589ull,
0x558b4820eb000000ull,
0xd00148f8458b48e0ull,
0xf8558b48f04d8b48ull,
0x28800b60fca0148ull,
0x458b4801f8458348ull,
0x48d672f8453948e8ull,
0x48f0458b48e8558bull,
0x458b480000c6d001ull,
0x4820ec8348c3c9f0ull,
0x2434894808247c89ull,
0x3ffba4824048b48ull,
0xf748ff803fe00ff8ull,
0xe0b8480ceac148e2ull,
0x480000007fc0000dull,
0xc0000de8ba481089ull,
0x24448b480000007full,
0x2444c74802894808ull,
0x4826eb0000000018ull,
0x7fc0000de8b8ull,
0x24548b48008b4800ull,
0xd0014803e2c14818ull,
0x480000000000c748ull,
0xe0b8480118244483ull,
0x480000007fc0000dull,
0x721824443948008bull,
0x7fc0000de8b848c6ull,
0xba48008b48000000ull,
0x7fc0000de0ull,
0x4803e2c148128b48ull,
0xc0000df0b848c201ull,
0x901089480000007full,
0xec8348c320c48348ull,
0x182444c74820ull,
0x102444c7480000ull,
0xe8b84861eb000000ull,
0x480000007fc0000dull,
0x481024548b48008bull,
0x8b48d0014803e2c1ull,
0x8348082444894800ull,
0xc74831740008247cull,
0x1eeb000000002404ull,
0x3e0830824448b48ull,
0x4483480674c08548ull,
0x124048348011824ull,
0x83480208246cc148ull,
0x9001ebdb761f243cull,
0xb848011024448348ull,
0x7fc0000de0ull,
0x1024443948008b48ull,
0x481824448b488b72ull,
0xc320c4834807e0c1ull,
0x7fc0000de0b848ull,
0xe0c148008b480000ull,
0x894848ec8348c30cull,
0xc0000df0b848243cull,
0x48008b480000007full,
0x243c83480774c085ull,
0xb80a7500ull,
0x48b4800000292e9ull,
0xc2894807e8c14824ull,
0x487fe08324048b48ull,
0xc0b60fc0950fc085ull,
0x4024448948d00148ull,
0x382444c748ull,
0x302444c74800ull,
0x24448b4818eb0000ull,
0xc0834802e0c14838ull,
0x8348382444894803ull,
0x24448b4801302444ull,
0x8734024443b4830ull,
0xd475ff38247c8348ull,
0x202444c748ull,
0x182444c74800ull,
0x48000001fbe90000ull,
0x244489484024448bull,
0xde8b84857eb28ull,
0x8b480000007fc0ull,
0xe2c1481824548b48ull,
0x48008b48d0014803ull,
0x1ad850fc085ull,
0x1e750020247c8348ull,
0x7fc0000df0b848ull,
0x548b48008b480000ull,
0x1480ce2c1481824ull,
0x83482024448948d0ull,
0x244483482028246cull,
0x2028247c83480118ull,
0x382444c748a177ull,
0x102444c748000000ull,
0x8b4818eb00000000ull,
0x4802e0c148382444ull,
0x382444894803c083ull,
0x8b48011024448348ull,
0x2824443b48102444ull,
0x82444c748dc72ull,
0x116e9000000ull,
0x7fc0000de8b848ull,
0x548b48008b480000ull,
0x14803e2c1481824ull,
0x24442348008b48d0ull,
0xd8850fc0854838ull,
0x482824548b480000ull,
0x48d001480824448bull,
0x1bac00101e883ull,
0x48e2d348c1890000ull,
0x483824443148d089ull,
0x482d750020247c83ull,
0x7fc0000df0b8ull,
0x24548b48008b4800ull,
0x5e1c148d1894818ull,
0xca01480824548b48ull,
0x48d0014807e2c148ull,
0xde8b84820244489ull,
0x8b480000007fc000ull,
0xc1481824548b4800ull,
0x8b48d0014803e2ull,
0x7fc0000de8ba48ull,
0x4c8b48128b480000ull,
0x14803e1c1481824ull,
0x89483824440b48caull,
0xde8b84830eb02ull,
0x8b480000007fc0ull,
0xe2c1481824548b48ull,
0xd0014808ea834803ull,
0x48ffffffff00c748ull,
0x6c83482040246c83ull,
0x40247c8348011824ull,
0x2024448b48c87720ull,
0x2040247c834865ebull,
0x108244483482677ull,
0x21b802382464c148ull,
0x2824442b48000000ull,
0xd5820f0824443948ull,
0x1eb9004ebfffffeull,
0x89484024448b4890ull,
0x202444c748282444ull,
0x2444834800000000ull,
0xc0000de0b8480118ull,
0x48008b480000007full,
0xfded820f18244439ull,
0x4800000000b8ffffull,
0xe5894855c348c483ull,
0xe87d894818ec8348ull,
0x48c78948e8458b48ull,
0x7fc0000472b8ull,
0x48f0458948d0ff00ull,
0xb8077500f07d83ull,
0x45c74851eb000000ull,
0x481feb00000000f8ull,
0xc5148d48f8458bull,
0x48f0458b48000000ull,
0xc748d001ull,
0x8b4801f845834800ull,
0x894803e8c148e845ull,
0x7e083e8458b48c2ull,
0xb60fc0950fc08548ull,
0xf8453948d00148c0ull,
0xc3c9f0458b48bd72ull,
0x243c894828ec8348ull,
0x7fc0000df0b848ull,
0xc08548008b480000ull,
0xb848000001db840full,
0x7fc0000df0ull,
0xf24043948008b48ull,
0xf0b848000001c482ull,
0x480000007fc0000dull,
0xc0000de0ba48008bull,
0x48128b480000007full,
0x4880c283480ce2c1ull,
0x820f24043b48d001ull,
0x24048b4800000195ull,
0x7fc0000df0ba48ull,
0xd02948128b480000ull,
0x448b482024448948ull,
0x4800000fff252024ull,
0x8948c0014807e8c1ull,
0x2024448b48182444ull,
0x244489480ce8c148ull,
0x3ba1824448b4820ull,
0xe2d348c189000000ull,
0x1024448948d08948ull,
0xc0000de8b84855ebull,
0x48008b480000007full,
0x3e2c1482024548bull,
0x8b48088b48d00148ull,
0x8948d0f748102444ull,
0x7fc0000de8b848c6ull,
0x8b48008b48000000ull,
0x4803e2c148202454ull,
0xca8948f12148d001ull,
0x102464c148108948ull,
0x4802182444834802ull,
0x483f740010247c83ull,
0x7fc0000de8b8ull,
0x24548b48008b4800ull,
0xd0014803e2c14820ull,
0x1024442348008b48ull,
0x1824448b48c28948ull,
0x48c18900000003beull,
0xc23948f08948e6d3ull,
0xb848ffffff64840full,
0x7fc0000de8ull,
0x2024548b48008b48ull,
0x48d0014803e2c148ull,
0x481024442348008bull,
0xbe1824448b48c289ull,
0xd348c18900000002ull,
0x75c23948f08948e6ull,
0x7fc0000de8b8484bull,
0x8b48008b48000000ull,
0x4803e2c148202454ull,
0x448b48088b48d001ull,
0xc68948d0f7481024ull,
0x7fc0000de8b848ull,
0x548b48008b480000ull,
0x14803e2c1482024ull,
0x48ca8948f12148d0ull,
0x244483481eeb1089ull,
0x3102444c7480120ull,
0x182444c748000000ull,
0xffff16e900000000ull,
0x55c328c4834890ffull,
0x4858ec8348e58948ull,
0x89b0758948b87d89ull,
0xc0000df0b848ac55ull,
0x48008b480000007full,
0xdf0b8483e74c085ull,
0x8b480000007fc000ull,
0x482b72b845394800ull,
0x7fc0000df0b8ull,
0xde0ba48008b4800ull,
0x8b480000007fc000ull,
0xc283480ce2c14812ull,
0xb8453b48d0014880ull,
0xe900000000b80a73ull,
0xb0458b4800000301ull,
0x48c2894807e8c148ull,
0x85487fe083b0458bull,
0x48c0b60fc0950fc0ull,
0x8b48c8458948d001ull,
0xc0000df0ba48b845ull,
0x48128b480000007full,
0x8b48f8458948d029ull,
0x4800000fff25f845ull,
0x8948c0014807e8c1ull,
0xc148f8458b48f045ull,
0xc748f84589480ce8ull,
0x45c600000000e845ull,
0x3baf0458b4800e7ull,
0xe2d348c189000000ull,
0xe9d8458948d08948ull,
0xe8458348000000beull,
0x48487400e77d8001ull,
0x7fc0000de8b8ull,
0xf8558b48008b4800ull,
0x48d0014803e2c148ull,
0xf748d8458b48088bull,
0xde8b848c68948d0ull,
0x8b480000007fc000ull,
0xe2c148f8558b4800ull,
0x48f12148d0014803ull,
0x4861eb108948ca89ull,
0x75c8453b48e8458bull,
0xe8b84801e745c657ull,
0x480000007fc0000dull,
0xc148f8558b48008bull,
0x308b48d0014803e2ull,
0x1baf0458b48ull,
0x8948e2d348c18900ull,
0x48c18948d0f748d0ull,
0x7fc0000de8b8ull,
0xf8558b48008b4800ull,
0x48d0014803e2c148ull,
0x108948f28948ce21ull,
0x45834802d865c148ull,
0x7400d87d834802f0ull,
0x7fc0000de8b8483cull,
0x8b48008b48000000ull,
0x14803e2c148f855ull,
0xd8452348008b48d0ull,
0xbef0458b48c28948ull,
0xd348c18900000003ull,
0xfc23948f08948e6ull,
0x7d8348fffffeff84ull,
0x156840f00d8ull,
0x7fc0000de8b848ull,
0x558b48008b480000ull,
0xd0014803e2c148f8ull,
0x48d8452348008b48ull,
0x2bef0458b48c289ull,
0xe6d348c189000000ull,
0x850fc23948f08948ull,
0xe84583480000011aull,
0x484b7400e77d8001ull,
0x7fc0000de8b8ull,
0xf8558b48008b4800ull,
0x48d0014803e2c148ull,
0xf748d8458b48088bull,
0xde8b848c68948d0ull,
0x8b480000007fc000ull,
0xe2c148f8558b4800ull,
0x48f12148d0014803ull,
0xdee9108948ca89ull,
0x3b48e8458b480000ull,
0xd0840fc845ull,
0x48c78948b0458b48ull,
0x7fc0000472b8ull,
0x48c0458948d0ff00ull,
0xeb00000000d045c7ull,
0x148d48d0458b4831ull,
0x458b4800000000c5ull,
0xd0558b48d00148b8ull,
0xd50c8d48ull,
0x48ca0148c0558b48ull,
0x458348028948008bull,
0xc148e8458b4801d0ull,
0x394803e8c14807e0ull,
0xac7d83bd72d045ull,
0xd0458b481feb2f74ull,
0xc5148d48ull,
0x48d00148c0458b48ull,
0x83480000000000c7ull,
0x48b0458b4801d045ull,
0x72d045394803e8c1ull,
0xc78948b8458b48d3ull,
0x7fc00007b8b848ull,
0xc0458b48d0ff0000ull,
0x83481aebb8458948ull,
0x3d845c74801f845ull,
0xf045c748000000ull,
0xfffffe42e9000000ull,
0x55c3c9b8458b4890ull,
0x4810ec8348e58948ull,
0x48f0758948f87d89ull,
0xbaf8458b48f04d8bull,
0x48ce894800000000ull,
0xc00009b7b848c789ull,
0xc3c9d0ff0000007full,
0x10ec8348e5894855ull,
0xf0758948f87d8948ull,
0xf8458b48f04d8b48ull,
0xce894800000001baull,
0x9b7b848c78948ull,
0xc9d0ff0000007fc0ull,
0xc3ull,
0x206d2749202c6948ull,
0x762749203b707061ull,
0x6570706f74732065ull,
0x6e692d6269662064ull,
0x6120687469772067ull,
0x646e61207525203aull,
0xa7525203a6220ull,
0xa7525203a61ull,
0x0ull,
0x0ull,
0x0ull,
0x0ull,
0x0ull, 0};

void* startApp(uint64_t stdout) {
    asm volatile("xchgw %ax, %ax");
    asm volatile("xchgw %bx, %bx");
    struct process *p = mallocz(sizeof(struct process));
    p->page = palloc();
    p->stdout = stdout;
    for (uint64_t i = 0; i < app_len; i++)
        ((uint64_t*) (p->page))[i] = app2[i];

    p->rip = 0x7FC0000000ull;
    p->rsp = 0x7FC0180000ull;

    pushListHead(runnableProcs, p); // TODO: I may have assumptions elsewhere that aren't met with this as is...
    return p;
}

void gotLine(struct process* proc, char* l) {
    
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

// So, let's see, I'd want to *not* share l2 table anymore, and instead make a new one per process, where the process sees
//   itself as beign an a fixed, predicatable address.  (Ah, and I think I finally get the higher-half kernel thing now, or
//   at least better than I had -- software interrupt puts me in ring 0, with my ring 0 stack (from tss) and cs from idt.  But
//   it doesn't restore our old cr3.  Kernel and user land both share the same page tables.  When we context switch (change
//   processes), we change cr3.  But software or hardware interrupt, while similar in some ways, is *not* a context switch, and
//   page tables remain the same.  So we map the kernel separate from any addresses the userland might use, because the kernel
//   sees the same addresses!
// This does require further thought and consideration...  When we're leaving the interrupt to go to the waitloop/scheduler, or
//   otherwise doing kernel work for the kernel, we go back to the default cr3?  No, friend.  Malloc and everything else sees
//   *consistent* higher-half addresesses, regardless of which process we are in.  The only addresses changing are lower-half,
//   userland stuff that only those processes need to worry about.  Kernel will see that userspace 2 MB page at *both* places --
//   the higher-half returned by malloc, and at the lower-half address we load everything at.  Because that's how we'll map
//   things in our page tables.
// So in the original, kernel-only tables, the kernel itself will be identity mapped in lower half (as it currently is) *and*
//   higher-half mapped as well, as it will be in all tables.  I guess that means we *still* have to make sure we'll compiled
//   with *only* relative addresses, right?  No absolute addresses other than what we explicitely use, like IDT, and stuff --
//   which raises the great question: which lower-half things need to be identity mapped?  Certainly first meg.  Well, again,
//   whole kernel will be, as all kernel code fits within first meg.  It's just things returned from malloc that are above
//   that.  Stack, heap.

// Hmm, seems hard to get GCC to use relative-only addressing; everything seems to be designed to not be runnable until made
//   fixed location.  (Linking required.)  That doesn't make sense to me, but whatever.  I guess the idea is lower-half setup
//   is fixed low; higher-half kernel is fixed high.  So maybe bootloader is lower half, and all of C code is higher-half, and
//   instead of falling straight through into C (still so cool!), we jump to higher-half address.  Easy to link that way.
// Do I need to care about physical alignment?  Can I just map my base higher-half address to exactly whatever address is
//   a label at bottom of bootloader assembly file, regardless of how it's aligned?

// Okay, but the question I was starting to ask was about what else needs to be identity mapped low, besides idt, gdtr, etc.
//   Kernel stack can be mapped both ways.  But what about page tables?  If I'm using tables I'm getting from malloc, I'm going
//   to see them as upper half.  But I'll need to load them in cr3 (l4, l3, l2) as physical addresses, right?
// Well, it would seem ugly on the one hand, but in reality not tricky, to translate to physical address simply by subtraction.
// We'll know the physical address of start of C code, and we'll know the virtual address, and that difference will be constant
//   across all mappings of virtual to physical, upper to lower.  So we can just use malloc and translate, as one option.

// Also note that Rust book finally answered I question I've had: I guess I want to call invlpg (“invalidate page”) when I update
//   pages, due to the translation lookaside buffer.

// Hmm, as I was going to bed, and very much as I was waking up, I realized I was jumping too far: I now have paging motivated,
//   and having kernel mapped in user process motivated, but the benefits of higher-half aren't clear to me.  Everything about
//   my process here is to have fun and do things the way that makes most sense to me, that feels fun and/or easy and/or
//   interesting and/or most right for me me here.

// And so what feels most obvious, fun, natural, and right to me here (which might soon be apparent as flawed, in which case we'd
//   have learned something!), is to have kernel in the lower half.  So it's always only dealing with physical addresses.  User
//   space can be high, though not necessarily upper half, just a consistent high location.

// Also, why did I switch from mapping 512 to mapping 256 GB?  I don't recall.  I recall doing it to solve some problem, but I
//   don't remember what the problem was...  Because I may want to put user processes at 256 GB, for example.

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
    printf("ip: 0x%p016h    cs: 0x%p016h flags: 0x%p016h\n", frame->ip, frame->cs, frame->flags);
    printf("sp: 0x%p016h    ss: 0x%p016h\n", frame->sp, frame->ss);
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

void __attribute__((interrupt)) int0x80_syscall(struct interrupt_frame *frame) {
    //print("int 0x80 handler\n");
    //dumpFrame(frame);

    if (frame->ip >= 511ull * 1024 * 1024 * 1024) {
        extern uint64_t regs[16];
        uint64_t* curRegs = (uint64_t*) curProc;
        curRegs++;
        for (int i = 0; i < 16; i++)
            curRegs[i] = regs[i];

        curProc->rip = frame->ip;
        curProc->rsp = frame->sp;

        switch (curProc->rax) {
        case 0: // exit()
            killProc(curProc);
            waitloop();
            break;
        case 1: // printf(char* fmt, va_list ap)
            no_ints(); // Printing will disable and then reenable, but we want them to stay off until iretq, so inc count of noes
            vaprintf(curProc->stdout, (char*) curProc->rbx, (va_list*) curProc->rcx);
            ints_okay_once_on(); // dec count of noes, so count is restored and iretq turns them on

            break;
        case 2: // printColor(char* s, color c)
            no_ints(); // Printing will disable and then reenable, but we want them to stay off until iretq, so inc count of noes
            printColorTo(curProc->stdout, (char*) curProc->rbx, (uint8_t) curProc->rcx); // This is a safe way to get just low 8-bits, right?
            ints_okay_once_on(); // dec count of noes, so count is restored and iretq turns them on

            break;
        case 3: // readline()
            // So my idea is to put the process into a "waiting for input" state, and when console reads a line on terminal for this process,
            //   if it's waiting for input, we put the line on the stack and iretq to it.
            curProc->wait = curProc->rax;
            removeNodeFromList(runnableProcs, curProcN);
            waitloop();
            break;
        default:
            logf("Unknown syscall 0x%h\n", curProc->rax);
        }
        // if (!curProc->rax) {
        //     killProc(curProc);
        //     waitloop();
        // }
    }

    //frame->ip = (uint64_t) waitloop;
    //print("waitloop?");
    //printf("hlt_in_waitloop in int 0x80: %u\n", hlt_in_waitloop);
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

    // if (pitCount % TICK_HZ == 0)
    //     logf("Average CPU ticks per PIT tick: %u\n", (read_tsc() - cpuCountOffset) / pitCount);

    if (periodicCallbacks.pcs) {
        for (uint64_t i = 0; i < periodicCallbacks.len; i++) {
            if (periodicCallbacks.pcs[i]->count == 0 || periodicCallbacks.pcs[i]->count > TICK_HZ) {
                logf("halting!\n");
                __asm__ __volatile__ ("hlt");
            }

            if (pitCount % (TICK_HZ * periodicCallbacks.pcs[i]->period / periodicCallbacks.pcs[i]->count) == 0)
                push(&wq, periodicCallbacks.pcs[i]->f);
        }
    }

    static uint64_t lms = 0;

    if (ms_since_boot % 2 == 0 && ms_since_boot != lms) {
        lms = ms_since_boot;

        if (frame->ip >= 511ull * 1024 * 1024 * 1024) {
            extern uint64_t regs[16];
            uint64_t* curRegs = (uint64_t*) curProc;
            curRegs++;
            for (int i = 0; i < 16; i++)
                curRegs[i] = regs[i];
            curProc->rip = frame->ip;
            curProc->rsp = frame->sp;

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
    //sleepingProcs = newList();
    //curProc = malloc(sizeof(struct process));

    //__asm__ __volatile__ ("xchgw %bx, %bx");
    ints_okay();
}
