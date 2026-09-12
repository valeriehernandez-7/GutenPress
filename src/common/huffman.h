#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stdint.h>
#include <stddef.h>
#include "min_heap.h"       // reuses HuffmanNode, defined there
#include "file_format.h"    // SYMBOL_ALPHABET_SIZE
#include "bit_stream.h"

typedef struct {
    uint64_t counts[SYMBOL_ALPHABET_SIZE];
} FrequencyTable;

// Scans the buffer once and tallies how many times each byte value
// appears. This table is the only input buildHuffmanTree needs.
void countByteFrequencies(const uint8_t* data, size_t dataLength, FrequencyTable* outTable);

// Builds the Huffman tree bottom-up from a frequency table, repeatedly
// combining the two least frequent nodes until one root remains.
// Returns NULL if the table has no symbols with a non-zero frequency
// (i.e. the input was empty).
HuffmanNode* buildHuffmanTree(const FrequencyTable* table);

void freeHuffmanTree(HuffmanNode* root);

// Walks the tree once to record how many bits each symbol's code uses.
// This is the only information saved to EntryHeader.codeLengths — the
// tree itself is discarded after this call.
void extractCodeLengths(const HuffmanNode* root, uint8_t outCodeLengths[SYMBOL_ALPHABET_SIZE]);

typedef struct {
    uint32_t code;
    uint8_t  length;   // 0 means "symbol not used"
} CanonicalCode;

// Regenerates the exact same bit patterns the encoder used, working
// only from code lengths — the standard canonical-Huffman trick that
// lets the decoder skip storing or rebuilding the tree.
void buildCanonicalCodes(const uint8_t codeLengths[SYMBOL_ALPHABET_SIZE], CanonicalCode outCodes[SYMBOL_ALPHABET_SIZE]);

// Writes 'dataLength' bytes to the bit stream using the given canonical
// codes. The caller already knows dataLength from the original file
// size, so no end-of-stream marker is needed here.
void encodeBuffer(const uint8_t* data, size_t dataLength,
                   const CanonicalCode codes[SYMBOL_ALPHABET_SIZE],
                   BitWriter* writer);

// Reconstructs exactly 'originalSize' bytes from the bit stream, using
// only the code lengths — the decoder never needs the original tree or
// the encoder's CanonicalCode table, just the lengths stored in the
// archive's EntryHeader.
void decodeBuffer(BitReader* reader,
                   const uint8_t codeLengths[SYMBOL_ALPHABET_SIZE],
                   uint64_t originalSize,
                   uint8_t* outBuffer);

#endif // HUFFMAN_H
