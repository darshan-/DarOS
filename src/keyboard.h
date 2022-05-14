#pragma once

#include <stdint.h>

void keyScanned(uint8_t c);
void registerKbdListener(void (*gotChar)(char));
