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
    com1_print("NEWLIST ----- 1\n");
    struct list* l = malloc(sizeof(struct list));
    l->head = (struct list_node*) 0;

    return l;
}

void addToList(struct list* l, void* item) {
    if (!l) return;

    com1_print("ADDTOLIST ----- 1\n");
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

// Removes first occurance of item from list, if it exists
void removeFromList(struct list* l, void* item) {
    if (!l || !l->head) return;

    struct list_node* prev = (struct list_node*) 0;
    for (struct list_node* cur = l->head; cur; prev = cur, cur = cur->next) {
        if (cur->item == item) {
            if (prev)
                prev->next = cur->next;
            else
                l->head = cur->next;
            free(cur);
            return;
        }
    }
}

void forEachListItem(struct list* l, void (*f)(void*)) {
    if (!l || !l->head) return;

    for (struct list_node* cur = l->head; cur; cur = cur->next)
        f(cur->item);
}

void destroyList() {
    // free() each item, then free() list
}
