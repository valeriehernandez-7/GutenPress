#include <stdio.h>
#include "huffman.h"

static void printBinary(uint32_t code, uint8_t length) {
    for (int i = length - 1; i >= 0; i--) {
        printf("%d", (code >> i) & 1);
    }
}

int main() {
    const char* sampleText = "aaaabbbccd";  // a:4 b:3 c:2 d:1

    FrequencyTable table;
    countByteFrequencies((const uint8_t*)sampleText, 10, &table);

    HuffmanNode* root = buildHuffmanTree(&table);

    uint8_t codeLengths[SYMBOL_ALPHABET_SIZE];
    extractCodeLengths(root, codeLengths);

    CanonicalCode codes[SYMBOL_ALPHABET_SIZE];
    buildCanonicalCodes(codeLengths, codes);

    const char* symbolsToShow = "abcd";
    for (int i = 0; symbolsToShow[i] != '\0'; i++) {
        uint8_t symbol = (uint8_t)symbolsToShow[i];
        printf("'%c' -> length %d, code ", symbol, codes[symbol].length);
        printBinary(codes[symbol].code, codes[symbol].length);
        printf("\n");
    }

    freeHuffmanTree(root);
    return 0;
}
