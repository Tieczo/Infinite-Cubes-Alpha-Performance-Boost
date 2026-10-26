// _mkdir for the native Linux build.
#pragma once
#include <sys/stat.h>
inline int _mkdir(const char *path) { return mkdir(path, 0755); }
