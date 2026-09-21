#ifndef FILE_SCANNER_H
#define FILE_SCANNER_H

#include <stddef.h>
#include <stdint.h>

// Holds both paths a caller needs for one file found on disk: the full
// path to actually open it, and the relative name to store in the
// archive so the original directory layout can be rebuilt later.
typedef struct {
  char *fullPath;
  char *relativeName;
} FileEntry;

// Lists every regular file directly inside directoryPath (no recursion,
// per current project scope). Uses stat() rather than dirent's d_type,
// since d_type is not guaranteed to be populated on every filesystem.
// Returns the number of files found, or -1 on error. On success,
// *outEntries is a newly allocated array the caller must release with
// freeFileEntryList.
int listRegularFilesInDirectory(const char *directoryPath,
                                FileEntry **outEntries);

void freeFileEntryList(FileEntry *entries, int entryCount);

// Loads a whole file into memory in one call. Simpler than streaming
// for this project's file sizes (Gutenberg books are at most a few MB),
// and both MD5 and Huffman need the complete bytes anyway. Returns
// NULL on failure.
uint8_t *readEntireFile(const char *filePath, size_t *outFileSize);

#endif // FILE_SCANNER_H
