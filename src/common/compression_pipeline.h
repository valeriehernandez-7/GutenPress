#ifndef COMPRESSION_PIPELINE_H
#define COMPRESSION_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Compresses one already-loaded file and writes its EntryHeader plus
// compressed payload to outputStream. MD5 is computed first, over the
// original bytes on disk (via fullPath), before Huffman touches them —
// this keeps the assignment's required order explicit in code, not
// just in a comment.
//
// outputStream can be any FILE*: a plain archive file (serial version),
// or a pipe wrapped with fdopen() (fork/IPC version) — the per-file
// work itself does not depend on how the work is scheduled or where
// its output ends up.
bool compressFileIntoStream(FILE *outputStream, const char *fullPath,
                            const char *relativeName, const uint8_t *fileData,
                            size_t fileSize, uint64_t *outCompressedSize);

#endif // COMPRESSION_PIPELINE_H
