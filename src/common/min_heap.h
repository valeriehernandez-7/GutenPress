#ifndef MIN_HEAP_H
#define MIN_HEAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// The heap grows by doubling, so this only needs to be big enough to
// avoid a reallocation on the very first inserts.
#define MIN_HEAP_INITIAL_CAPACITY 16

typedef struct HuffmanNode HuffmanNode;
struct HuffmanNode {
    uint8_t      symbol;     // meaningful only when isLeaf is true
    uint64_t     frequency;
    bool         isLeaf;
    HuffmanNode* left;
    HuffmanNode* right;
};

// A binary min-heap ordered by frequency, used as the priority queue
// that drives Huffman tree construction: the two least frequent nodes
// must always be cheap to find.
typedef struct {
    HuffmanNode** nodes;
    size_t        count;
    size_t        capacity;
} MinHeap;

void         minHeapInit(MinHeap* heap);
void         minHeapPush(MinHeap* heap, HuffmanNode* node);

// Removes and returns the node with the lowest frequency. Caller must
// not call this on an empty heap (check minHeapSize first).
HuffmanNode* minHeapPop(MinHeap* heap);

size_t       minHeapSize(const MinHeap* heap);

// Frees the heap's internal array only. Node ownership passes to
// whoever pops them, so this never frees HuffmanNode pointers.
void         minHeapFree(MinHeap* heap);

#endif // MIN_HEAP_H
