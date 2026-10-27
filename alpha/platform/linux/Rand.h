// rand and srand as in Microsoft's C runtime, which the Windows builds use (platform/linux/Rand.cpp,
// linked in place of glibc's with --wrap, cmake/Linux.cmake): the same sequence for a seed (the world
// generation srands a seed for every region and zone, so the landscape, its trees and its items come
// out as on Windows) and a state of their own in every thread (several zones are generated at once,
// each after its own srand). glibc's rand is another generator, shared by all threads. This header,
// included ahead of every file of the game, gives RAND_MAX its Windows value.
#pragma once
#include <stdlib.h>

#undef RAND_MAX
#define RAND_MAX 0x7fff
