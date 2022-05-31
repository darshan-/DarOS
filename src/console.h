#pragma once

#include <stdint.h>

void clearScreen();
void print(char* s);
void printColor(char* s, uint8_t c);
void printc(char c);
void printf(char* fmt, ...);
void updateMemUse();
void startTty();
