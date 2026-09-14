#ifndef PIPE_BUFFER_H
#define PIPE_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#define PIPE_BUFFER_INITIAL_CAPACITY 65536

// Accumulates bytes read from one worker's pipe into a growable buffer.
// Each worker's output must stay in its own buffer — never interleaved
// with another worker's — because poll()-driven reads can return a
// partial EntryHeader/payload record at an arbitrary byte boundary;
// mixing two workers' raw bytes into one buffer would corrupt records.
typedef struct {
    uint8_t* data;
    size_t   length;
    size_t   capacity;
} PipeBuffer;

void pipeBufferInit(PipeBuffer* buffer);
void pipeBufferAppend(PipeBuffer* buffer, const uint8_t* newData, size_t newDataLength);
void pipeBufferFree(PipeBuffer* buffer);

#endif // PIPE_BUFFER_H
