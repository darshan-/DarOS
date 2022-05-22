#pragma once

struct list;

struct list* newList();
void addToList(struct list* l, void* item);
void removeFromList(struct list* l, void* item);
void forEachListItem(struct list* l, void (*f)(void*));

// Only to be called when interrupts are enabled (or safe to reenable).
// For code that touches data structures that interrupt handlers also touch.
void* atomicPop(struct list* l);
