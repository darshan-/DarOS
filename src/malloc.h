#pragma once

#include <stdint.h>

void init_heap(uint64_t size);
void* malloc(uint64_t nBytes);
void free(void*);
void* realloc(void* p, int newSize);
