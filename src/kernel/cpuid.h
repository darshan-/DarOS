#pragma once

#include <stdint.h>

struct cpuid_ret {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

struct cpuid_ret cpuid(uint32_t eax);
