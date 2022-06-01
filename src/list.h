#pragma once

#include <stdint.h>

struct list;

struct list* newList();
void pushListHead(struct list* l, void* item);
void pushListTail(struct list* l, void* item);
void* popListHead(struct list* l);
void removeFromList(struct list* l, void* item);
void removeFromListWithEquality(struct list* l, int (*equals)(void*));
void forEachListItem(struct list* l, void (*f)(void*));
uint32_t listLen(struct list* l);
