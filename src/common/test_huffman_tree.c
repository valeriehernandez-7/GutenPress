#include "huffman.h"
#include <stdio.h>

int main() {
  const char *sampleText = "aaaabbbccd"; // a:4 b:3 c:2 d:1

  FrequencyTable table;
  countByteFrequencies((const uint8_t *)sampleText, 10, &table);

  printf("Frequencies -> a:%llu b:%llu c:%llu d:%llu\n",
         (unsigned long long)table.counts['a'],
         (unsigned long long)table.counts['b'],
         (unsigned long long)table.counts['c'],
         (unsigned long long)table.counts['d']);

  HuffmanNode *root = buildHuffmanTree(&table);
  printf("Root frequency (should equal total length, 10): %llu\n",
         (unsigned long long)root->frequency);
  printf("Root is leaf (should be 0/false, since more than one symbol exists): "
         "%d\n",
         root->isLeaf);

  freeHuffmanTree(root);
  return 0;
}
