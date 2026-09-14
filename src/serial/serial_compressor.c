// Enables POSIX extensions (dirent.h, sys/stat.h, strdup) even when
// compiling with the strict -std=c11 flag used across this project.
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../common/file_format.h"
#include "../common/md5_utils.h"
#include "../common/huffman.h"
#include "../common/bit_stream.h"
#include "../common/file_scanner.h"
#include "../common/compression_pipeline.h"


int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <inputDirectory> <outputArchivePath>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *inputDirectory = argv[1];
    const char *outputArchivePath = argv[2];

    FileEntry *fileEntries;
    int fileCount = listRegularFilesInDirectory(inputDirectory, &fileEntries);
    if (fileCount < 0)
    {
        return EXIT_FAILURE;
    }
    if (fileCount == 0)
    {
        fprintf(stderr, "No regular files found in '%s'\n", inputDirectory);
        return EXIT_FAILURE;
    }

    FILE *archiveFile = fopen(outputArchivePath, "wb");
    if (archiveFile == NULL)
    {
        fprintf(stderr, "Could not create archive '%s'\n", outputArchivePath);
        freeFileEntryList(fileEntries, fileCount);
        return EXIT_FAILURE;
    }

    ArchiveHeader archiveHeader;
    memcpy(archiveHeader.magicNumber, ARCHIVE_MAGIC_NUMBER, ARCHIVE_MAGIC_LENGTH);
    archiveHeader.formatVersion = ARCHIVE_FORMAT_VERSION;
    archiveHeader.entryCount = (uint32_t)fileCount;
    writeArchiveHeader(archiveFile, &archiveHeader);

    uint64_t totalOriginalSize = 0;
    uint64_t totalCompressedSize = 0;
    int successfulCount = 0;

    for (int i = 0; i < fileCount; i++)
    {
        size_t fileSize;
        uint8_t *fileData = readEntireFile(fileEntries[i].fullPath, &fileSize);
        if (fileData == NULL)
        {
            printf("[%d/%d] %s -> FAILED (read error)\n", i + 1, fileCount, fileEntries[i].relativeName);
            continue;
        }

        uint64_t compressedSize = 0;
        bool wasSuccessful = compressFileIntoStream(archiveFile,
                                                    fileEntries[i].fullPath,
                                                    fileEntries[i].relativeName,
                                                    fileData, fileSize,
                                                    &compressedSize);
        if (wasSuccessful)
        {
            successfulCount++;
            totalOriginalSize += fileSize;
            totalCompressedSize += compressedSize;
        }

        printf("[%d/%d] %s -> %s\n", i + 1, fileCount, fileEntries[i].relativeName,
               wasSuccessful ? "OK" : "FAILED");

        free(fileData);
    }

    fclose(archiveFile);
    freeFileEntryList(fileEntries, fileCount);

    double compressionRatio = (totalOriginalSize > 0)
                                  ? (double)totalCompressedSize / (double)totalOriginalSize
                                  : 0.0;

    printf("\n--- Summary ---\n");
    printf("Files compressed: %d/%d\n", successfulCount, fileCount);
    printf("Total original size: %llu bytes\n", (unsigned long long)totalOriginalSize);
    printf("Total compressed size: %llu bytes\n", (unsigned long long)totalCompressedSize);
    printf("Compression ratio: %.4f\n", compressionRatio);

    return (successfulCount == fileCount) ? EXIT_SUCCESS : EXIT_FAILURE;
}
