#include "huffman.h"

#include <stdlib.h>

void countByteFrequencies(const uint8_t* data, size_t dataLength, FrequencyTable* outTable) {
    for (int symbol = 0; symbol < SYMBOL_ALPHABET_SIZE; symbol++) {
        outTable->counts[symbol] = 0;
    }

    for (size_t i = 0; i < dataLength; i++) {
        outTable->counts[data[i]]++;
    }
}

static HuffmanNode* makeLeafNode(uint8_t symbol, uint64_t frequency) {
    HuffmanNode* node = malloc(sizeof(HuffmanNode));
    node->symbol = symbol;
    node->frequency = frequency;
    node->isLeaf = true;
    node->left = NULL;
    node->right = NULL;
    return node;
}

static HuffmanNode* makeInternalNode(HuffmanNode* left, HuffmanNode* right) {
    HuffmanNode* node = malloc(sizeof(HuffmanNode));
    node->symbol = 0;   // unused for internal nodes
    node->frequency = left->frequency + right->frequency;
    node->isLeaf = false;
    node->left = left;
    node->right = right;
    return node;
}

HuffmanNode* buildHuffmanTree(const FrequencyTable* table) {
    MinHeap heap;
    minHeapInit(&heap);

    int distinctSymbolCount = 0;
    for (int symbol = 0; symbol < SYMBOL_ALPHABET_SIZE; symbol++) {
        if (table->counts[symbol] > 0) {
            minHeapPush(&heap, makeLeafNode((uint8_t)symbol, table->counts[symbol]));
            distinctSymbolCount++;
        }
    }

    if (distinctSymbolCount == 0) {
        minHeapFree(&heap);
        return NULL;   // empty input: nothing to build a tree from
    }

    // A tree with a single leaf would give that symbol a 0-bit code,
    // which is not decodable. Pairing it with a zero-frequency dummy
    // leaf forces a real 1-bit code without affecting compressed size
    // for files that use more than one distinct byte.
    if (distinctSymbolCount == 1) {
        HuffmanNode* onlyLeaf = minHeapPop(&heap);
        HuffmanNode* dummyLeaf = makeLeafNode(0, 0);
        minHeapPush(&heap, makeInternalNode(onlyLeaf, dummyLeaf));
    } else {
        while (minHeapSize(&heap) > 1) {
            HuffmanNode* first = minHeapPop(&heap);
            HuffmanNode* second = minHeapPop(&heap);
            minHeapPush(&heap, makeInternalNode(first, second));
        }
    }

    HuffmanNode* root = minHeapPop(&heap);
    minHeapFree(&heap);
    return root;
}

void freeHuffmanTree(HuffmanNode* root) {
    if (root == NULL) {
        return;
    }
    freeHuffmanTree(root->left);
    freeHuffmanTree(root->right);
    free(root);
}

static void walkTreeForCodeLengths(const HuffmanNode* node, uint8_t depth,
                                    uint8_t outCodeLengths[SYMBOL_ALPHABET_SIZE]) {
    if (node->isLeaf) {
        // depth 0 can only happen when the whole tree is a single leaf,
        // which buildHuffmanTree already prevents by pairing it with a
        // dummy leaf — so every real symbol gets at least 1 bit.
        outCodeLengths[node->symbol] = depth;
        return;
    }

    walkTreeForCodeLengths(node->left, (uint8_t)(depth + 1), outCodeLengths);
    walkTreeForCodeLengths(node->right, (uint8_t)(depth + 1), outCodeLengths);
}

void extractCodeLengths(const HuffmanNode* root, uint8_t outCodeLengths[SYMBOL_ALPHABET_SIZE]) {
    for (int symbol = 0; symbol < SYMBOL_ALPHABET_SIZE; symbol++) {
        outCodeLengths[symbol] = 0;   // 0 marks "symbol not present"
    }
    walkTreeForCodeLengths(root, 0, outCodeLengths);
}

// Symbols are ordered by (codeLength, symbol value) because that is the
// order canonical Huffman assigns codes in: shortest codes first, and
// ties broken by symbol value so encoder and decoder always agree.
static int compareBySymbolThenLength(const void* firstPointer, const void* secondPointer, uint8_t codeLengths[SYMBOL_ALPHABET_SIZE]) {
    int first = *(const int*)firstPointer;
    int second = *(const int*)secondPointer;

    if (codeLengths[first] != codeLengths[second]) {
        return codeLengths[first] - codeLengths[second];
    }
    return first - second;
}

// qsort's comparator can't take extra context directly, so this small
// wrapper reaches the code-length table through a file-scope pointer
// set right before sorting.
static uint8_t* codeLengthsForSort;
static int qsortCompare(const void* firstPointer, const void* secondPointer) {
    return compareBySymbolThenLength(firstPointer, secondPointer, codeLengthsForSort);
}

void buildCanonicalCodes(const uint8_t codeLengths[SYMBOL_ALPHABET_SIZE],
                          CanonicalCode outCodes[SYMBOL_ALPHABET_SIZE]) {
    int usedSymbols[SYMBOL_ALPHABET_SIZE];
    int usedSymbolCount = 0;

    for (int symbol = 0; symbol < SYMBOL_ALPHABET_SIZE; symbol++) {
        outCodes[symbol].code = 0;
        outCodes[symbol].length = codeLengths[symbol];
        if (codeLengths[symbol] > 0) {
            usedSymbols[usedSymbolCount++] = symbol;
        }
    }

    codeLengthsForSort = (uint8_t*)codeLengths;
    qsort(usedSymbols, usedSymbolCount, sizeof(int), qsortCompare);

    uint32_t nextCode = 0;
    uint8_t previousLength = usedSymbolCount > 0 ? codeLengths[usedSymbols[0]] : 0;

    for (int i = 0; i < usedSymbolCount; i++) {
        int symbol = usedSymbols[i];
        uint8_t currentLength = codeLengths[symbol];

        // Every time the code length grows, the running code must shift
        // left to make room for the extra bit(s) — this is what keeps
        // canonical codes prefix-free without ever inspecting the tree.
        nextCode <<= (currentLength - previousLength);

        outCodes[symbol].code = nextCode;
        nextCode++;
        previousLength = currentLength;
    }
}

void encodeBuffer(const uint8_t* data, size_t dataLength,
                   const CanonicalCode codes[SYMBOL_ALPHABET_SIZE],
                   BitWriter* writer) {
    for (size_t i = 0; i < dataLength; i++) {
        uint8_t symbol = data[i];
        bitWriterWriteBits(writer, codes[symbol].code, codes[symbol].length);
    }
}

// Canonical codes can be decoded without rebuilding the tree: at each
// bit length, symbols occupy a contiguous range of code values. This
// walks bit lengths from shortest to longest, tracking that range the
// same way buildCanonicalCodes assigned it, until the accumulated code
// falls inside the range for the current length.
void decodeBuffer(BitReader* reader,
                   const uint8_t codeLengths[SYMBOL_ALPHABET_SIZE],
                   uint64_t originalSize,
                   uint8_t* outBuffer) {
    // Rebuilding the (code -> symbol) mapping once up front is simpler
    // and less error-prone than re-deriving ranges per bit during
    // decoding, at a negligible memory cost (at most 256 entries).
    CanonicalCode codes[SYMBOL_ALPHABET_SIZE];
    buildCanonicalCodes(codeLengths, codes);

    uint64_t decodedCount = 0;
    while (decodedCount < originalSize) {
        uint32_t accumulatedCode = 0;
        uint8_t accumulatedLength = 0;
        bool symbolFound = false;

        // Longest canonical code needed for a single file never exceeds
        // the alphabet size in practice, so 32 bits is a safe ceiling.
        while (accumulatedLength < 32 && !symbolFound) {
            int bitValue = bitReaderReadBit(reader);
            accumulatedCode = (accumulatedCode << 1) | (uint32_t)bitValue;
            accumulatedLength++;

            for (int symbol = 0; symbol < SYMBOL_ALPHABET_SIZE; symbol++) {
                if (codes[symbol].length == accumulatedLength &&
                    codes[symbol].code == accumulatedCode) {
                    outBuffer[decodedCount++] = (uint8_t)symbol;
                    symbolFound = true;
                    break;
                }
            }
        }
    }
}
