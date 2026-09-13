#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>

#include "../common/file_format.h"
#include "../common/md5_utils.h"
#include "../common/huffman.h"
#include "../common/bit_stream.h"

#define MAX_PATH_LENGTH 4096

// Creates directoryPath and any missing parent directories, the same
// way "mkdir -p" does. Archives are flat today, but entry filenames
// could carry subpaths in the future without this function needing to
// change at all.
static bool ensureDirectoryExists(const char* directoryPath) {
    char pathBuffer[MAX_PATH_LENGTH];
    snprintf(pathBuffer, sizeof(pathBuffer), "%s", directoryPath);

    for (char* cursor = pathBuffer + 1; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(pathBuffer, 0755) != 0 && errno != EEXIST) {
                fprintf(stderr, "Could not create directory '%s'\n", pathBuffer);
                return false;
            }
            *cursor = '/';
        }
    }

    if (mkdir(pathBuffer, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Could not create directory '%s'\n", pathBuffer);
        return false;
    }

    return true;
}

// Reconstructs one archive entry's original bytes on disk, then
// recomputes MD5 over the freshly written file and compares it against
// the digest stored in the entry header — the "verify after
// decompression" step the assignment requires.
static bool decompressEntry(FILE* archiveFile,
                             const EntryHeader* entryHeader,
                             const char* outputDirectoryPath,
                             bool* outMd5Matched) {
    *outMd5Matched = false;

    uint8_t* compressedBuffer = NULL;
    if (entryHeader->compressedSize > 0) {
        compressedBuffer = malloc(entryHeader->compressedSize);
        if (compressedBuffer == NULL) {
            fprintf(stderr, "Out of memory while decompressing '%s'\n", entryHeader->filename);
            return false;
        }
        if (fread(compressedBuffer, 1, entryHeader->compressedSize, archiveFile) != entryHeader->compressedSize) {
            fprintf(stderr, "Truncated archive while reading '%s'\n", entryHeader->filename);
            free(compressedBuffer);
            return false;
        }
    }

    uint8_t* decodedBuffer = NULL;
    if (entryHeader->originalSize > 0) {
        decodedBuffer = malloc(entryHeader->originalSize);
        if (decodedBuffer == NULL) {
            fprintf(stderr, "Out of memory while decoding '%s'\n", entryHeader->filename);
            free(compressedBuffer);
            return false;
        }

        BitReader reader;
        bitReaderInit(&reader, compressedBuffer, entryHeader->compressedSize);
        decodeBuffer(&reader, entryHeader->codeLengths, entryHeader->originalSize, decodedBuffer);
    }

    char outputFilePath[MAX_PATH_LENGTH];
    snprintf(outputFilePath, sizeof(outputFilePath), "%s/%s", outputDirectoryPath, entryHeader->filename);

    FILE* outputFile = fopen(outputFilePath, "wb");
    if (outputFile == NULL) {
        fprintf(stderr, "Could not create output file '%s'\n", outputFilePath);
        free(compressedBuffer);
        free(decodedBuffer);
        return false;
    }

    bool writeSucceeded = true;
    if (entryHeader->originalSize > 0) {
        writeSucceeded = fwrite(decodedBuffer, 1, entryHeader->originalSize, outputFile) == entryHeader->originalSize;
    }
    fclose(outputFile);

    free(compressedBuffer);
    free(decodedBuffer);

    if (!writeSucceeded) {
        fprintf(stderr, "Failed to write '%s'\n", outputFilePath);
        return false;
    }

    uint8_t recomputedDigest[MD5_DIGEST_LENGTH];
    if (!computeFileMd5(outputFilePath, recomputedDigest)) {
        return false;
    }

    *outMd5Matched = digestsAreEqual(recomputedDigest, entryHeader->md5Digest);
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <inputArchivePath> <outputDirectory>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* inputArchivePath = argv[1];
    const char* outputDirectory = argv[2];

    FILE* archiveFile = fopen(inputArchivePath, "rb");
    if (archiveFile == NULL) {
        fprintf(stderr, "Could not open archive '%s'\n", inputArchivePath);
        return EXIT_FAILURE;
    }

    ArchiveHeader archiveHeader;
    if (!readArchiveHeader(archiveFile, &archiveHeader)) {
        fprintf(stderr, "'%s' is not a valid archive produced by this program\n", inputArchivePath);
        fclose(archiveFile);
        return EXIT_FAILURE;
    }

    if (!ensureDirectoryExists(outputDirectory)) {
        fclose(archiveFile);
        return EXIT_FAILURE;
    }

    int extractedCount = 0;
    int verifiedCount = 0;

    for (uint32_t i = 0; i < archiveHeader.entryCount; i++) {
        EntryHeader entryHeader;
        if (!readEntryHeader(archiveFile, &entryHeader)) {
            fprintf(stderr, "Archive appears truncated after %u/%u entries\n", i, archiveHeader.entryCount);
            break;
        }

        bool md5Matched = false;
        bool wasSuccessful = decompressEntry(archiveFile, &entryHeader, outputDirectory, &md5Matched);

        if (wasSuccessful) {
            extractedCount++;
            if (md5Matched) {
                verifiedCount++;
            }
        }

        printf("[%u/%u] %s -> %s (MD5 %s)\n",
               i + 1, archiveHeader.entryCount, entryHeader.filename,
               wasSuccessful ? "OK" : "FAILED",
               md5Matched ? "verified" : "MISMATCH");

        freeEntryHeader(&entryHeader);
    }

    fclose(archiveFile);

    double healthPercentage = (archiveHeader.entryCount > 0)
        ? (100.0 * (double)verifiedCount / (double)archiveHeader.entryCount)
        : 0.0;

    printf("\n--- Summary ---\n");
    printf("Files extracted: %d/%u\n", extractedCount, archiveHeader.entryCount);
    printf("MD5 verified: %d/%u\n", verifiedCount, archiveHeader.entryCount);
    printf("Compression health: %.2f%%\n", healthPercentage);

    return (verifiedCount == (int)archiveHeader.entryCount) ? EXIT_SUCCESS : EXIT_FAILURE;
}
