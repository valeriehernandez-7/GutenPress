#include "bit_stream.h"

#include <stdio.h>
#include <stdlib.h>

// A course-project-scale corpus never approaches the point where doubling
// forever would be a problem, so we keep growth logic simple: fail loudly
// instead of adding recovery paths that would never be exercised.
static void ensureWriterCapacity(BitWriter *writer) {
  if (writer->byteCount < writer->byteCapacity) {
    return;
  }

  size_t newCapacity = writer->byteCapacity * 2;
  uint8_t *newBuffer = realloc(writer->buffer, newCapacity);
  if (newBuffer == NULL) {
    fprintf(stderr, "bit_stream: out of memory while growing buffer\n");
    exit(EXIT_FAILURE);
  }

  writer->buffer = newBuffer;
  writer->byteCapacity = newCapacity;
}

void bitWriterInit(BitWriter *writer) {
  writer->buffer = malloc(BIT_STREAM_INITIAL_CAPACITY);
  if (writer->buffer == NULL) {
    fprintf(stderr, "bit_stream: out of memory during initialization\n");
    exit(EXIT_FAILURE);
  }

  writer->byteCapacity = BIT_STREAM_INITIAL_CAPACITY;
  writer->byteCount = 0;
  writer->currentByte = 0;
  writer->bitsInCurrentByte = 0;
}

void bitWriterWriteBit(BitWriter *writer, int bitValue) {
  writer->currentByte = (uint8_t)((writer->currentByte << 1) | (bitValue & 1));
  writer->bitsInCurrentByte++;

  if (writer->bitsInCurrentByte == 8) {
    ensureWriterCapacity(writer);
    writer->buffer[writer->byteCount++] = writer->currentByte;
    writer->currentByte = 0;
    writer->bitsInCurrentByte = 0;
  }
}

void bitWriterWriteBits(BitWriter *writer, uint32_t code, int bitLength) {
  for (int bitPosition = bitLength - 1; bitPosition >= 0; bitPosition--) {
    int bitValue = (code >> bitPosition) & 1;
    bitWriterWriteBit(writer, bitValue);
  }
}

uint8_t bitWriterFlush(BitWriter *writer) {
  if (writer->bitsInCurrentByte == 0) {
    return 0;
  }

  uint8_t paddingBits = (uint8_t)(8 - writer->bitsInCurrentByte);
  writer->currentByte = (uint8_t)(writer->currentByte << paddingBits);

  ensureWriterCapacity(writer);
  writer->buffer[writer->byteCount++] = writer->currentByte;

  writer->currentByte = 0;
  writer->bitsInCurrentByte = 0;
  return paddingBits;
}

void bitWriterFree(BitWriter *writer) {
  free(writer->buffer);
  writer->buffer = NULL;
}

void bitReaderInit(BitReader *reader, const uint8_t *buffer, size_t byteCount) {
  reader->buffer = buffer;
  reader->byteCount = byteCount;
  reader->currentByteIndex = 0;
  reader->bitsReadInCurrentByte = 0;
}

int bitReaderReadBit(BitReader *reader) {
  if (reader->currentByteIndex >= reader->byteCount) {
    return -1;
  }

  uint8_t currentByte = reader->buffer[reader->currentByteIndex];
  int shiftAmount = 7 - reader->bitsReadInCurrentByte;
  int bitValue = (currentByte >> shiftAmount) & 1;

  reader->bitsReadInCurrentByte++;
  if (reader->bitsReadInCurrentByte == 8) {
    reader->bitsReadInCurrentByte = 0;
    reader->currentByteIndex++;
  }

  return bitValue;
}
