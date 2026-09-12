#include "min_heap.h"

#include <stdio.h>
#include <stdlib.h>

static void swapNodes(HuffmanNode** first, HuffmanNode** second) {
    HuffmanNode* temporary = *first;
    *first = *second;
    *second = temporary;
}

static void ensureCapacity(MinHeap* heap) {
    if (heap->count < heap->capacity) {
        return;
    }

    size_t newCapacity = heap->capacity * 2;
    HuffmanNode** newNodes = realloc(heap->nodes, newCapacity * sizeof(HuffmanNode*));
    if (newNodes == NULL) {
        fprintf(stderr, "min_heap: out of memory while growing heap\n");
        exit(EXIT_FAILURE);
    }

    heap->nodes = newNodes;
    heap->capacity = newCapacity;
}

// Restores the heap property upward from a freshly inserted leaf.
static void siftUp(MinHeap* heap, size_t index) {
    while (index > 0) {
        size_t parentIndex = (index - 1) / 2;
        if (heap->nodes[parentIndex]->frequency <= heap->nodes[index]->frequency) {
            break;
        }
        swapNodes(&heap->nodes[parentIndex], &heap->nodes[index]);
        index = parentIndex;
    }
}

// Restores the heap property downward after the root is replaced.
static void siftDown(MinHeap* heap, size_t index) {
    while (true) {
        size_t leftChild = (2 * index) + 1;
        size_t rightChild = (2 * index) + 2;
        size_t smallest = index;

        if (leftChild < heap->count &&
            heap->nodes[leftChild]->frequency < heap->nodes[smallest]->frequency) {
            smallest = leftChild;
        }
        if (rightChild < heap->count &&
            heap->nodes[rightChild]->frequency < heap->nodes[smallest]->frequency) {
            smallest = rightChild;
        }

        if (smallest == index) {
            break;
        }

        swapNodes(&heap->nodes[index], &heap->nodes[smallest]);
        index = smallest;
    }
}

void minHeapInit(MinHeap* heap) {
    heap->nodes = malloc(MIN_HEAP_INITIAL_CAPACITY * sizeof(HuffmanNode*));
    if (heap->nodes == NULL) {
        fprintf(stderr, "min_heap: out of memory during initialization\n");
        exit(EXIT_FAILURE);
    }

    heap->count = 0;
    heap->capacity = MIN_HEAP_INITIAL_CAPACITY;
}

void minHeapPush(MinHeap* heap, HuffmanNode* node) {
    ensureCapacity(heap);
    heap->nodes[heap->count] = node;
    siftUp(heap, heap->count);
    heap->count++;
}

HuffmanNode* minHeapPop(MinHeap* heap) {
    HuffmanNode* smallestNode = heap->nodes[0];

    heap->count--;
    heap->nodes[0] = heap->nodes[heap->count];
    siftDown(heap, 0);

    return smallestNode;
}

size_t minHeapSize(const MinHeap* heap) {
    return heap->count;
}

void minHeapFree(MinHeap* heap) {
    free(heap->nodes);
    heap->nodes = NULL;
}
