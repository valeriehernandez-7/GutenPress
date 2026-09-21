#ifndef DIRECTORY_UTILS_H
#define DIRECTORY_UTILS_H

#include <stdbool.h>

// Creates directoryPath and any missing parent directories, the same
// way "mkdir -p" does. Used by every decompressor version before
// writing extracted files into a directory that may not exist yet.
bool ensureDirectoryExists(const char *directoryPath);

#endif // DIRECTORY_UTILS_H
