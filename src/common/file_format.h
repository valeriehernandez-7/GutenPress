#ifndef FILE_FORMAT_H
#define FILE_FORMAT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Identifies files produced by this program and rejects unrelated files
// early, before attempting to parse anything after it.
#define ARCHIVE_MAGIC_NUMBER "HUFC"
#define ARCHIVE_MAGIC_LENGTH 4

// Lets the format evolve later without breaking archives created by
// earlier versions of the compressor.
#define ARCHIVE_FORMAT_VERSION 1

// Canonical Huffman only needs the bit-length of each symbol's code, not
// the full tree, so the table below has one entry per possible byte value.
#define SYMBOL_ALPHABET_SIZE 256

// MD5 digests are always 16 bytes, regardless of input size.
#define MD5_DIGEST_LENGTH 16

// Written once at the very start of the archive.
typedef struct {
  char magicNumber[ARCHIVE_MAGIC_LENGTH];
  uint8_t formatVersion;
  uint32_t entryCount; // how many original files this archive holds
} ArchiveHeader;

// Written once per original file, immediately before that file's
// compressed bit stream.
typedef struct {
  uint16_t filenameLength;
  char *filename;                            // relative path, so folder
                                             // structure can be rebuilt
  uint64_t originalSize;                     // bytes before compression
  uint64_t compressedSize;                   // bytes of Huffman-coded data
  uint8_t md5Digest[MD5_DIGEST_LENGTH];      // computed BEFORE compression
  uint8_t codeLengths[SYMBOL_ALPHABET_SIZE]; // 0 means "symbol not used"
  uint8_t paddingBits;                       // unused bits in the last
                                             // byte of the bit stream
} EntryHeader;

// Field-by-field serialization avoids relying on the compiler's struct
// padding, which is not guaranteed to match between different builds —
// writing a raw struct with fwrite would be fragile across compilers
// or optimization levels, even on the same machine.

bool writeArchiveHeader(FILE *outputFile, const ArchiveHeader *header);

// Returns false (without reporting further errors) if the magic number
// does not match, so callers can reject foreign files immediately.
bool readArchiveHeader(FILE *inputFile, ArchiveHeader *outHeader);

bool writeEntryHeader(FILE *outputFile, const EntryHeader *header);

// Allocates header->filename; caller must release it with freeEntryHeader.
bool readEntryHeader(FILE *inputFile, EntryHeader *outHeader);

void freeEntryHeader(EntryHeader *header);

#endif // FILE_FORMAT_H
