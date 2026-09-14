#include "pipe_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pipeBufferInit(PipeBuffer* buffer) {
    buffer->data = malloc(PIPE_BUFFER_INITIAL_CAPACITY);
    if (buffer->data == NULL) {
        fprintf(stderr, "pipe_buffer: out of memory during initialization\n");
        exit(EXIT_FAILURE);
    }
    buffer->length = 0;
    buffer->capacity = PIPE_BUFFER_INITIAL_CAPACITY;
}

void pipeBufferAppend(PipeBuffer* buffer, const uint8_t* newData, size_t newDataLength) {
    size_t requiredCapacity = buffer->length + newDataLength;

    while (buffer->capacity < requiredCapacity) {
        buffer->capacity *= 2;
    }

    uint8_t* grownData = realloc(buffer->data, buffer->capacity);
    if (grownData == NULL) {
        fprintf(stderr, "pipe_buffer: out of memory while growing buffer\n");
        exit(EXIT_FAILURE);
    }
    buffer->data = grownData;

    memcpy(buffer->data + buffer->length, newData, newDataLength);
    buffer->length += newDataLength;
}

void pipeBufferFree(PipeBuffer* buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}
