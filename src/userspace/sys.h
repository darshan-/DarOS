#pragma once

#include <stdint.h>

void print(char* s);
void printf(char* fmt, ...);
void printColor(char* s, uint8_t c);
char* M_readline();

void exit();
void wait(uint64_t pid);
uint64_t runProg(char* s);

extern uint64_t stdout;
