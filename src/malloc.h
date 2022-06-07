#pragma once

#include <stdint.h>

void init_heap(uint64_t* start, uint64_t size);
void* malloc(uint64_t nBytes);
void free(void*);
void* realloc(void* p, int newSize);
uint64_t memUsed();
uint64_t heapSize();
