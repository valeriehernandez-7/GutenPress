#include "min_heap.h"
#include <stdio.h>
#include <stdlib.h>

static HuffmanNode *makeLeaf(uint8_t symbol, uint64_t frequency) {
  HuffmanNode *node = malloc(sizeof(HuffmanNode));
  node->symbol = symbol;
  node->frequency = frequency;
  node->isLeaf = true;
  node->left = NULL;
  node->right = NULL;
  return node;
}

int main() {
  MinHeap heap;
  minHeapInit(&heap);

  uint64_t frequencies[] = {50, 10, 40, 5, 30, 20};
  for (int i = 0; i < 6; i++) {
    minHeapPush(&heap, makeLeaf((uint8_t)('a' + i), frequencies[i]));
  }

  printf("Extraction order (should be ascending): ");
  while (minHeapSize(&heap) > 0) {
    HuffmanNode *node = minHeapPop(&heap);
    printf("%llu ", (unsigned long long)node->frequency);
    free(node);
  }
  printf("\n");

  minHeapFree(&heap);
  return 0;
}
