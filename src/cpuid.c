#include <stdint.h>

#include "cpuid.h"

//uint64_t cpuid(uint16_t eax) {
struct cpuid_ret cpuid(uint32_t eax) {
    struct cpuid_ret ret;

    __asm__ __volatile__(
        //"mov $0, %%eax\n"
        "mov %0, %%eax\n"
        "cpuid\n"
        "mov %%eax, %1\n"
        "mov %%ebx, %2\n"
        "mov %%ecx, %3\n"
        "mov %%edx, %4\n"
        // "mov %%eax, %1\n"
        // :"=m"(eax), "=m"(id)
        :"=m"(eax), "=m"(ret.eax), "=m"(ret.ebx), "=m"(ret.ecx), "=m"(ret.edx)
    );

    return ret;
}

