#pragma once

#include <stdint.h>

void print(char* s);
void printf(char* fmt, ...);
void printColor(char* s, uint8_t c);

void exit();
void wait(uint64_t pid);
uint64_t runProg(char* s);
