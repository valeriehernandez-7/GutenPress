#include "huffman.h"
#include <stdio.h>
#include <string.h>

int main() {
  const char *originalText = "the quick brown fox jumps over the lazy dog";
  size_t textLength = strlen(originalText);

  FrequencyTable table;
  countByteFrequencies((const uint8_t *)originalText, textLength, &table);

  HuffmanNode *root = buildHuffmanTree(&table);

  uint8_t codeLengths[SYMBOL_ALPHABET_SIZE];
  extractCodeLengths(root, codeLengths);

  CanonicalCode codes[SYMBOL_ALPHABET_SIZE];
  buildCanonicalCodes(codeLengths, codes);

  BitWriter writer;
  bitWriterInit(&writer);
  encodeBuffer((const uint8_t *)originalText, textLength, codes, &writer);
  uint8_t paddingBits = bitWriterFlush(&writer);

  printf("Original size: %zu bytes\n", textLength);
  printf("Compressed size: %zu bytes (padding: %u bits)\n", writer.byteCount,
         paddingBits);

  BitReader reader;
  bitReaderInit(&reader, writer.buffer, writer.byteCount);

  uint8_t decodedBuffer[256];
  decodeBuffer(&reader, codeLengths, textLength, decodedBuffer);
  decodedBuffer[textLength] = '\0';

  printf("Decoded text: %s\n", decodedBuffer);

  if (memcmp(originalText, decodedBuffer, textLength) == 0) {
    printf("SUCCESS: decoded text matches original byte-for-byte.\n");
  } else {
    printf("FAILURE: decoded text does NOT match original.\n");
  }

  bitWriterFree(&writer);
  freeHuffmanTree(root);
  return 0;
}
