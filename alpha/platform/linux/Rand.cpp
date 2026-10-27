// See Rand.h.
#include "Rand.h"

#include <stdint.h>

namespace {
thread_local uint32_t t_holdRand = 1; // a new thread starts as after srand(1), as on Windows
}

extern "C" int __wrap_rand()
{
    t_holdRand = t_holdRand * 214013u + 2531011u;
    return (int)((t_holdRand >> 16) & 0x7fff);
}

extern "C" void __wrap_srand(unsigned int seed)
{
    t_holdRand = seed;
}
