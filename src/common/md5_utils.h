#ifndef MD5_UTILS_H
#define MD5_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include "file_format.h"

// Reads and hashes a file in fixed-size chunks instead of loading it
// fully into memory, so this works the same way for a small text file
// and for a multi-megabyte one.
#define MD5_READ_BUFFER_SIZE 8192

// Computes the MD5 digest of the file at filePath and writes it into
// digestOutput (must point to MD5_DIGEST_LENGTH bytes). Returns true on
// success, false if the file could not be read or hashing failed.
bool computeFileMd5(const char* filePath, uint8_t digestOutput[MD5_DIGEST_LENGTH]);

// Compares two digests byte-by-byte. Used to verify a file after
// decompression against the digest stored at compression time.
bool digestsAreEqual(const uint8_t firstDigest[MD5_DIGEST_LENGTH],
                      const uint8_t secondDigest[MD5_DIGEST_LENGTH]);

// Renders a digest as a lowercase hex string (33 bytes: 32 hex chars +
// null terminator). Useful for logging and for the report's screenshots.
void digestToHexString(const uint8_t digest[MD5_DIGEST_LENGTH], char outputHex[33]);

#endif // MD5_UTILS_H
