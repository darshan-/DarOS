#pragma once

#include <stdint.h>

void hexoutByte(uint8_t b, void (*printChar)(uint8_t));
void hexoutWord(uint16_t w, void (*printChar)(uint8_t));
void hexoutDword(uint32_t d, void (*printChar)(uint8_t));
void hexoutQword(uint64_t q, void (*printChar)(uint8_t));
