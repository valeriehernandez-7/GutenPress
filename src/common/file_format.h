#ifndef FILE_FORMAT_H
#define FILE_FORMAT_H

#include <stdint.h>

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
    char     magicNumber[ARCHIVE_MAGIC_LENGTH];
    uint8_t  formatVersion;
    uint32_t entryCount;   // how many original files this archive holds
} ArchiveHeader;

// Written once per original file, immediately before that file's
// compressed bit stream.
typedef struct {
    uint16_t filenameLength;
    char*    filename;                          // relative path, so folder
                                                  // structure can be rebuilt
    uint64_t originalSize;                       // bytes before compression
    uint64_t compressedSize;                     // bytes of Huffman-coded data
    uint8_t  md5Digest[MD5_DIGEST_LENGTH];       // computed BEFORE compression
    uint8_t  codeLengths[SYMBOL_ALPHABET_SIZE];  // 0 means "symbol not used"
    uint8_t  paddingBits;                        // unused bits in the last
                                                  // byte of the bit stream
} EntryHeader;

#endif // FILE_FORMAT_H
