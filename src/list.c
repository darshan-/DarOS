#include "list.h"
#include "malloc.h"

struct list_node {
    void* item;
    struct list_node* next;
};

struct list {
    struct list_node* head;
};

struct list* newList() {
    struct list* l = malloc(sizeof(struct list));
    l->head = (struct list_node*) 0;

    return l;
}

void addToList(struct list* l, void* item) {
    if (!l) return;

    struct list_node* n = malloc(sizeof(struct list_node));
    n->item = item;
    n->next = (struct list_node*) 0;

    if (!l->head) {
        l->head = n;
        return;
    }

    struct list_node* cur = l->head;
    while (cur->next)
        cur = cur->next;
    cur->next = n;
}

void* atomicPop(struct list* l) {
    void* item = (void *) 0;

    __asm__ __volatile__("cli");

    if (!l || !l->head) goto ret;

    item = l->head->item;

    struct list_node* old_head = l->head;
    l->head = l->head->next;
    free(old_head);

 ret:
    __asm__ __volatile__("sti");
    return item;
}

void removeFromListWithEquality(struct list* l, int (*equals)(void*)) {
    if (!l || !l->head) return;

    struct list_node* prev = (struct list_node*) 0;
    for (struct list_node* cur = l->head; cur; prev = cur, cur = cur->next) {
        if (equals(cur->item)) {
            if (prev)
                prev->next = cur->next;
            else
                l->head = cur->next;

            free(cur);

            return;
        }
    }
}

// Removes first occurance of item from list, if it exists
void removeFromList(struct list* l, void* item) {
    removeFromListWithEquality(l, ({
        int __fn__ (void* other) {
            return other == item;
        }

        __fn__;
    }));
}

// void removeFromList(struct list* l, void* item) {
//     removeFromListWithEquality(l, item, ({
//         uint64_t __fn__ (void* l, void* r) {
//             return l == r;
//         }

//         __fn__;
//     }));
// }

// void removeFromListWithEquality(struct list* l, void* item, uint64_t (*equals)(void*, void*)) {
//     if (!l || !l->head) return;

//     struct list_node* prev = (struct list_node*) 0;
//     for (struct list_node* cur = l->head; cur; prev = cur, cur = cur->next) {
//         if (equals(cur->item, item)) {
//             if (prev)
//                 prev->next = cur->next;
//             else
//                 l->head = cur->next;

//             free(cur);

//             return;
//         }
//     }
// }

void forEachListItem(struct list* l, void (*f)(void*)) {
    if (!l || !l->head) return;

    for (struct list_node* cur = l->head; cur; cur = cur->next)
        f(cur->item);
}

void destroyList() {
    // free() each item, then free() list
}
