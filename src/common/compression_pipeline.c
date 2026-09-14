#include "compression_pipeline.h"

#include <string.h>

#include "file_format.h"
#include "md5_utils.h"
#include "huffman.h"
#include "bit_stream.h"

bool compressFileIntoStream(FILE* outputStream,
                             const char* fullPath,
                             const char* relativeName,
                             const uint8_t* fileData,
                             size_t fileSize,
                             uint64_t* outCompressedSize) {
    uint8_t md5Digest[MD5_DIGEST_LENGTH];
    if (!computeFileMd5(fullPath, md5Digest)) {
        return false;
    }

    FrequencyTable frequencyTable;
    countByteFrequencies(fileData, fileSize, &frequencyTable);

    HuffmanNode* huffmanTreeRoot = buildHuffmanTree(&frequencyTable);

    uint8_t codeLengths[SYMBOL_ALPHABET_SIZE] = {0};
    CanonicalCode codes[SYMBOL_ALPHABET_SIZE] = {0};

    // buildHuffmanTree only returns NULL for a zero-byte file — in that
    // case codeLengths stays all zero and there is nothing to encode,
    // which is still a valid (empty) entry rather than an error.
    if (huffmanTreeRoot != NULL) {
        extractCodeLengths(huffmanTreeRoot, codeLengths);
        buildCanonicalCodes(codeLengths, codes);
    }

    BitWriter writer;
    bitWriterInit(&writer);

    if (huffmanTreeRoot != NULL) {
        encodeBuffer(fileData, fileSize, codes, &writer);
    }
    uint8_t paddingBits = bitWriterFlush(&writer);

    EntryHeader entryHeader;
    entryHeader.filenameLength = (uint16_t)strlen(relativeName);
    entryHeader.filename = (char*)relativeName;
    entryHeader.originalSize = (uint64_t)fileSize;
    entryHeader.compressedSize = (uint64_t)writer.byteCount;
    memcpy(entryHeader.md5Digest, md5Digest, MD5_DIGEST_LENGTH);
    memcpy(entryHeader.codeLengths, codeLengths, SYMBOL_ALPHABET_SIZE);
    entryHeader.paddingBits = paddingBits;

    bool headerWritten = writeEntryHeader(outputStream, &entryHeader);
    bool dataWritten = fwrite(writer.buffer, 1, writer.byteCount, outputStream) == writer.byteCount;

    *outCompressedSize = writer.byteCount;

    if (huffmanTreeRoot != NULL) {
        freeHuffmanTree(huffmanTreeRoot);
    }
    bitWriterFree(&writer);

    return headerWritten && dataWritten;
}
