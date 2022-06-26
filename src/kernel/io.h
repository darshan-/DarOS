#pragma once

#include <stdint.h>

void outb(uint32_t dest, uint8_t val);
void outw(uint32_t dest, uint16_t val);
void outd(uint32_t dest, uint32_t val);
uint8_t inb(uint32_t source);
uint16_t inw(uint32_t source);
uint32_t ind(uint32_t source);
