#pragma once

struct list;

struct list* newList();
void addToList(struct list* l, void* item);
void removeFromList(struct list* l, void* item);
void forEachListItem(struct list* l, void (*f)(void*));
