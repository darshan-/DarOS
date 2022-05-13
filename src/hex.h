#pragma once

#include <stdint.h>

void byteToHex(uint8_t b, char* s);
void wordToHex(uint16_t w, char* s);
void dwordToHex(uint32_t d, char* s);
void qwordToHex(uint64_t q, char* s);
