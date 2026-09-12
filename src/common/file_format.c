#include "file_format.h"

#include <stdlib.h>
#include <string.h>

// This project targets a single known environment (Debian 13, x86_64),
// so byte order never varies between the machine that compresses and
// the one that decompresses — no endianness conversion is needed here.

bool writeArchiveHeader(FILE* outputFile, const ArchiveHeader* header) {
    if (fwrite(header->magicNumber, 1, ARCHIVE_MAGIC_LENGTH, outputFile) != ARCHIVE_MAGIC_LENGTH) {
        return false;
    }
    if (fwrite(&header->formatVersion, sizeof(header->formatVersion), 1, outputFile) != 1) {
        return false;
    }
    if (fwrite(&header->entryCount, sizeof(header->entryCount), 1, outputFile) != 1) {
        return false;
    }
    return true;
}

bool readArchiveHeader(FILE* inputFile, ArchiveHeader* outHeader) {
    if (fread(outHeader->magicNumber, 1, ARCHIVE_MAGIC_LENGTH, inputFile) != ARCHIVE_MAGIC_LENGTH) {
        return false;
    }
    if (memcmp(outHeader->magicNumber, ARCHIVE_MAGIC_NUMBER, ARCHIVE_MAGIC_LENGTH) != 0) {
        return false;   // not a file this program produced
    }
    if (fread(&outHeader->formatVersion, sizeof(outHeader->formatVersion), 1, inputFile) != 1) {
        return false;
    }
    if (fread(&outHeader->entryCount, sizeof(outHeader->entryCount), 1, inputFile) != 1) {
        return false;
    }
    return true;
}

bool writeEntryHeader(FILE* outputFile, const EntryHeader* header) {
    if (fwrite(&header->filenameLength, sizeof(header->filenameLength), 1, outputFile) != 1) {
        return false;
    }
    if (fwrite(header->filename, 1, header->filenameLength, outputFile) != header->filenameLength) {
        return false;
    }
    if (fwrite(&header->originalSize, sizeof(header->originalSize), 1, outputFile) != 1) {
        return false;
    }
    if (fwrite(&header->compressedSize, sizeof(header->compressedSize), 1, outputFile) != 1) {
        return false;
    }
    if (fwrite(header->md5Digest, 1, MD5_DIGEST_LENGTH, outputFile) != MD5_DIGEST_LENGTH) {
        return false;
    }
    if (fwrite(header->codeLengths, 1, SYMBOL_ALPHABET_SIZE, outputFile) != SYMBOL_ALPHABET_SIZE) {
        return false;
    }
    if (fwrite(&header->paddingBits, sizeof(header->paddingBits), 1, outputFile) != 1) {
        return false;
    }
    return true;
}

bool readEntryHeader(FILE* inputFile, EntryHeader* outHeader) {
    if (fread(&outHeader->filenameLength, sizeof(outHeader->filenameLength), 1, inputFile) != 1) {
        return false;
    }

    // +1 for the null terminator, so callers can use the filename with
    // ordinary string functions (fopen, snprintf, etc.) without tracking
    // filenameLength separately everywhere.
    outHeader->filename = malloc(outHeader->filenameLength + 1);
    if (outHeader->filename == NULL) {
        return false;
    }
    if (fread(outHeader->filename, 1, outHeader->filenameLength, inputFile) != outHeader->filenameLength) {
        free(outHeader->filename);
        return false;
    }
    outHeader->filename[outHeader->filenameLength] = '\0';

    if (fread(&outHeader->originalSize, sizeof(outHeader->originalSize), 1, inputFile) != 1) {
        free(outHeader->filename);
        return false;
    }
    if (fread(&outHeader->compressedSize, sizeof(outHeader->compressedSize), 1, inputFile) != 1) {
        free(outHeader->filename);
        return false;
    }
    if (fread(outHeader->md5Digest, 1, MD5_DIGEST_LENGTH, inputFile) != MD5_DIGEST_LENGTH) {
        free(outHeader->filename);
        return false;
    }
    if (fread(outHeader->codeLengths, 1, SYMBOL_ALPHABET_SIZE, inputFile) != SYMBOL_ALPHABET_SIZE) {
        free(outHeader->filename);
        return false;
    }
    if (fread(&outHeader->paddingBits, sizeof(outHeader->paddingBits), 1, inputFile) != 1) {
        free(outHeader->filename);
        return false;
    }

    return true;
}

void freeEntryHeader(EntryHeader* header) {
    free(header->filename);
    header->filename = NULL;
}
