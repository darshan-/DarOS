#pragma once

#include <stdint.h>

void init_heap(uint64_t* start, uint64_t size);
void* malloc(uint64_t nBytes);
void* palloc();
void* mallocz(uint64_t nBytes);
void free(void*);
void* realloc(void* p, uint64_t newSize);
void* reallocz(void* p, uint64_t newSize);
uint64_t heapUsed();
uint64_t heapSize();
