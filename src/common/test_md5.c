#include <stdio.h>
#include "md5_utils.h"

int main() {
    uint8_t digest[MD5_DIGEST_LENGTH];
    const char* testFilePath = "../gutenberg_corpus/pride_and_prejudice_by_jane_austen_(177409)-1342.txt";

    if (!computeFileMd5(testFilePath, digest)) {
        return 1;
    }

    char hexString[33];
    digestToHexString(digest, hexString);
    printf("MD5: %s\n", hexString);
    return 0;
}
