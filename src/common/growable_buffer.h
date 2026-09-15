#ifndef GROWABLE_BUFFER_H
#define GROWABLE_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#define GROWABLE_BUFFER_INITIAL_CAPACITY 65536

// Accumulates bytes appended over time into a buffer that doubles its
// capacity as needed. Used by both the fork()+IPC and pthread workers
// to collect one worker's compressed output in memory before it gets
// merged into the final archive — each worker owns its own buffer, so
// no synchronization is needed while appending to it.
typedef struct {
    uint8_t* data;
    size_t   length;
    size_t   capacity;
} GrowableBuffer;

void growableBufferInit(GrowableBuffer* buffer);
void growableBufferAppend(GrowableBuffer* buffer, const uint8_t* newData, size_t newDataLength);
void growableBufferFree(GrowableBuffer* buffer);

#endif // GROWABLE_BUFFER_H
