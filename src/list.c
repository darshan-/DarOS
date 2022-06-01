#include <stdint.h>

#include "list.h"

#include "interrupt.h"
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

// Do I want to return -1 if list doesn't exist?  For now, let's treat null list as empty...
uint8_t listIsEmpty(struct list* l) {
    return !l || !l->head;
}

void* popListHead(struct list* l) {
    if (!l || !l->head) return 0;

    void* ret = l->head->item;
    struct list_node* oldh = l->head;
    l->head = l->head->next;
    free(oldh);

    return ret;
}

void pushListHead(struct list* l, void* item) {
    if (!l) return;

    struct list_node* n = malloc(sizeof(struct list_node));
    n->item = item;
    n->next = l->head;

    l->head = n;
}

void pushListTail(struct list* l, void* item) {
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

void forEachListItem(struct list* l, void (*f)(void*)) {
    if (!l || !l->head) return;

    for (struct list_node* cur = l->head; cur; cur = cur->next)
        f(cur->item);
}

void destroyList() {
    // free() each item, then free() list
}
