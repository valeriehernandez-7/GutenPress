#ifndef BIT_STREAM_H
#define BIT_STREAM_H

#include <stdint.h>
#include <stddef.h>

// The writer doubles its buffer whenever it runs out of room, so callers
// never need to know the final compressed size ahead of time.
#define BIT_STREAM_INITIAL_CAPACITY 4096

// Accumulates individual bits (most significant bit first) into a
// growable byte buffer. Used while encoding a file into its Huffman
// bit stream.
typedef struct {
    uint8_t* buffer;
    size_t   byteCapacity;
    size_t   byteCount;          // complete bytes written so far
    uint8_t  currentByte;        // bits being assembled before they fill a byte
    int      bitsInCurrentByte;
} BitWriter;

// Reads individual bits back out of a byte buffer produced by a
// BitWriter, in the same order they were written. Used while decoding
// a Huffman bit stream.
typedef struct {
    const uint8_t* buffer;
    size_t         byteCount;
    size_t         currentByteIndex;
    int            bitsReadInCurrentByte;
} BitReader;

// --- Writer ------------------------------------------------------------

void bitWriterInit(BitWriter* writer);
void bitWriterWriteBit(BitWriter* writer, int bitValue);

// Writes the lowest 'bitLength' bits of 'code', most significant bit
// first — the order canonical Huffman codes are naturally generated in.
void bitWriterWriteBits(BitWriter* writer, uint32_t code, int bitLength);

// Pads the last partial byte with zero bits so the stream ends on a byte
// boundary. Returns how many padding bits were added (0-7); the caller
// stores this in the entry header purely for reporting purposes, since
// decoding stops by symbol count, not by padding detection.
uint8_t bitWriterFlush(BitWriter* writer);

void bitWriterFree(BitWriter* writer);

// --- Reader ------------------------------------------------------------

void bitReaderInit(BitReader* reader, const uint8_t* buffer, size_t byteCount);

// Returns 0 or 1, or -1 if the buffer has been fully consumed.
int bitReaderReadBit(BitReader* reader);

#endif // BIT_STREAM_H
