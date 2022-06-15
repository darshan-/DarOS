#pragma once

#include <stdint.h>

#define LOGS_TERM 0

void print(char* s);
void printColor(char* s, uint8_t c);
void printc(char c);
void printf(char* fmt, ...);
void printColorTo(uint8_t t, char* s, uint8_t c);
void printTo(uint8_t t, char* s);
void startTty();
