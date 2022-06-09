#include <stdint.h>

#include "list.h"

#include "interrupt.h"
#include "malloc.h"

struct list_node {
    void* item;
    struct list_node* next;
    struct list_node* prev;
};

struct list {
    struct list_node* head;
    struct list_node* tail;
    uint32_t len;
};

struct list* newList() {
    struct list* l = malloc(sizeof(struct list));
    l->head = (struct list_node*) 0;
    l->tail = (struct list_node*) 0;
    l-> len = 0;

    return l;
}

// Do I want to return -1 if list doesn't exist?  For now, let's treat null list as empty...
uint32_t listLen(struct list* l) {
    if (!l) return 0;

    return l->len;
}

void* listHead(struct list* l) {
    if (!l) return 0;

    return l->head;
}

void* nextNode(void* ln) {
    if (!ln) return 0;

    return ((struct list_node*) ln)->next;
}

void* listItem(void* ln) {
    if (!ln) return 0;

    return ((struct list_node*) ln)->item;
}

void* popListHead(struct list* l) {
    if (!l || !l->head) return 0;

    l->len--;
    void* ret = l->head->item;
    struct list_node* oldh = l->head;
    l->head = l->head->next;

    if (l->head)
        l->head->prev = 0;
    else
        l->tail = 0;

    free(oldh);

    return ret;
}

void* pushListHead(struct list* l, void* item) {
    if (!l) return 0;

    l->len++;
    struct list_node* n = malloc(sizeof(struct list_node));
    n->item = item;
    n->next = l->head;
    n->prev = 0;

    if (l->head)
        l->head->prev = n;
    else
        l->tail = n;

    l->head = n;

    return n;
}

void* pushListTail(struct list* l, void* item) {
    if (!l) return 0;

    l->len++;
    struct list_node* n = malloc(sizeof(struct list_node));
    n->item = item;
    n->next = (struct list_node*) 0;
    n->prev = l->tail;

    if (l->tail)
        l->tail->next = n;
    else
        l->head = n;

    l->tail = n;

    return n;
}

void removeFromListWithEquality(struct list* l, int (*equals)(void*)) {
    if (!l || !l->head) return;

    l->len--;
    for (struct list_node* cur = l->head; cur; cur = cur->next) {
        if (equals(cur->item)) {
            if (cur->prev)
                cur->prev->next = cur->next;
            if (cur->next)
                cur->next->prev = cur->prev;

            if (cur == l->head)
                l->head = cur->next;
            if (cur == l->tail)
                l->tail = cur->prev;

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

static void* forEachLI(struct list_node* n, void (*f)(void*)) {
    struct list_node* last = (void*) 0;
    for (; n; n = n->next) {
        last = n;
        f(n->item);
    }

    return last;
}

void* forEachListItem(struct list* l, void (*f)(void*)) {
    if (!l || !l->head) return (void*) 0;

    return forEachLI(l->head, f);
}

void* forEachNewListItem(void* last, void (*f)(void*)) {
    if (!last) return (void*) 0;

    void* ret = forEachLI(((struct list_node*) last)->next, f);
    if (!ret) ret = last;

    return ret;
}

void destroyList(struct list* l) {
    if (!l) return;

    while(l->head) {
        struct list_node* next = l->head->next;
        free(l->head->item);
        free(l->head);
        l->head = next;
    }

    free(l);
}
