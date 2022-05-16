#pragma once

#include <stdint.h>

#define VRAM 0xb8000

void clearScreen();
void print(char* s);
void printColor(char* s, uint8_t c);
void printc(char c);
void printf(char* fmt, ...);
