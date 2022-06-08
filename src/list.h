#pragma once

#include <stdint.h>

struct list;

struct list* newList();
uint32_t listLen(struct list* l);
void* nextNode(void*);
void* nodeItem(void*);
void* pushListHead(struct list* l, void* item);
void* pushListTail(struct list* l, void* item);
void* popListHead(struct list* l);
void removeFromList(struct list* l, void* item);
void removeFromListWithEquality(struct list* l, int (*equals)(void*));
void* forEachListItem(struct list* l, void (*f)(void*));
void* forEachNewListItem(void* ln, void (*f)(void*));
void destroyList(struct list* l);
