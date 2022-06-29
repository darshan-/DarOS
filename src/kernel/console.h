#pragma once

#include <stdarg.h>
#include <stdint.h>

#define LOGS_TERM 0

void print(char* s);
void printColor(char* s, uint8_t c);
void printc(char c);
void printf(char* fmt, ...);
void printColorTo(uint64_t t, char* s, uint8_t c);
void printTo(uint64_t t, char* s);
void startTty();
void vaprintf(uint64_t t, char* fmt, va_list* ap);
void procDone(void* p, uint64_t t);
