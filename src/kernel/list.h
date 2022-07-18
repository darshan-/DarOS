#pragma once

#include <stdint.h>

struct list;

struct list* newList();
uint64_t listLen(struct list* l);
void* listHead(struct list* l);
void* listTail(struct list* l);
void* nextNode(void*);
void* nextNodeCirc(struct list* l, void* ln);
void* prevNode(void*);
void* listItem(void*);
void* pushListHead(struct list* l, void* item);
void* pushListTail(struct list* l, void* item);
void* popListHead(struct list* l);
void removeNodeFromList(struct list* l, void* n);
void removeFromList(struct list* l, void* item);
void removeFromListWithEquality(struct list* l, int (*equals)(void*));
void* getNodeByCondition(struct list* l, int (*matches)(void*));
void* forEachListItem(struct list* l, void (*f)(void*));
void* forEachNewListItem(void* ln, void (*f)(void*));
void destroyList(struct list* l);
