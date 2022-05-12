#pragma once

#include <stdint.h>

#define VRAM 0xb8000

void clearScreen();
void print(char* s);
void printColor(char* s, uint8_t c);
void printByte(uint8_t b);
void printWord(uint16_t w);
void printDword(uint32_t d);
void printQword(uint64_t q);
