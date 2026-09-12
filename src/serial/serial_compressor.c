// Enables POSIX extensions (dirent.h, sys/stat.h, strdup) even when
// compiling with the strict -std=c11 flag used across this project.
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>

#include "../common/file_format.h"
#include "../common/md5_utils.h"
#include "../common/huffman.h"
#include "../common/bit_stream.h"

#define MAX_PATH_LENGTH 4096

typedef struct {
    char* fullPath;       // needed to open and read the file's bytes
    char* relativeName;   // stored in the archive so it can be rebuilt later
} FileEntry;

// Lists every regular file directly inside directoryPath (no recursion,
// per current project scope). Uses stat() rather than dirent's d_type,
// since d_type is not guaranteed to be populated on every filesystem.
static int listRegularFilesInDirectory(const char* directoryPath, FileEntry** outEntries) {
    DIR* directoryStream = opendir(directoryPath);
    if (directoryStream == NULL) {
        fprintf(stderr, "Could not open directory '%s'\n", directoryPath);
        return -1;
    }

    // A first pass counts matching entries so the array can be
    // allocated once at its exact size, instead of growing it entry
    // by entry with repeated reallocations.
    int fileCount = 0;
    struct dirent* entry;
    while ((entry = readdir(directoryStream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char entryPath[MAX_PATH_LENGTH];
        snprintf(entryPath, sizeof(entryPath), "%s/%s", directoryPath, entry->d_name);

        struct stat entryStat;
        if (stat(entryPath, &entryStat) == 0 && S_ISREG(entryStat.st_mode)) {
            fileCount++;
        }
    }

    if (fileCount == 0) {
        closedir(directoryStream);
        *outEntries = NULL;
        return 0;
    }

    FileEntry* entries = malloc((size_t)fileCount * sizeof(FileEntry));
    if (entries == NULL) {
        fprintf(stderr, "Out of memory while listing '%s'\n", directoryPath);
        closedir(directoryStream);
        return -1;
    }

    rewinddir(directoryStream);
    int entryIndex = 0;
    while ((entry = readdir(directoryStream)) != NULL && entryIndex < fileCount) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char entryPath[MAX_PATH_LENGTH];
        snprintf(entryPath, sizeof(entryPath), "%s/%s", directoryPath, entry->d_name);

        struct stat entryStat;
        if (stat(entryPath, &entryStat) == 0 && S_ISREG(entryStat.st_mode)) {
            entries[entryIndex].fullPath = strdup(entryPath);
            entries[entryIndex].relativeName = strdup(entry->d_name);
            entryIndex++;
        }
    }

    closedir(directoryStream);
    *outEntries = entries;
    return fileCount;
}

static void freeFileEntryList(FileEntry* entries, int entryCount) {
    for (int i = 0; i < entryCount; i++) {
        free(entries[i].fullPath);
        free(entries[i].relativeName);
    }
    free(entries);
}

// Loads a whole file into memory in one call. Simpler than streaming
// for this project's file sizes (Gutenberg books are at most a few MB),
// and both MD5 and Huffman need the complete bytes anyway.
static uint8_t* readEntireFile(const char* filePath, size_t* outFileSize) {
    FILE* file = fopen(filePath, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file '%s'\n", filePath);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (fileSize < 0) {
        fprintf(stderr, "Could not determine size of '%s'\n", filePath);
        fclose(file);
        return NULL;
    }

    uint8_t* buffer = malloc((size_t)fileSize);
    if (buffer == NULL) {
        fprintf(stderr, "Out of memory while reading '%s'\n", filePath);
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, (size_t)fileSize, file) != (size_t)fileSize) {
        fprintf(stderr, "Failed to read full contents of '%s'\n", filePath);
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *outFileSize = (size_t)fileSize;
    return buffer;
}

// Compresses one already-loaded file and appends its EntryHeader plus
// compressed payload to the archive. MD5 is computed first, over the
// original bytes, before anything Huffman-related touches them — this
// keeps the assignment's required order ("firma MD5 ... antes de
// comprimirlo") explicit in the code, not just in a comment.
static bool compressFileIntoArchive(FILE* archiveFile,
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

    bool headerWritten = writeEntryHeader(archiveFile, &entryHeader);
    bool dataWritten = fwrite(writer.buffer, 1, writer.byteCount, archiveFile) == writer.byteCount;

    *outCompressedSize = writer.byteCount;

    if (huffmanTreeRoot != NULL) {
        freeHuffmanTree(huffmanTreeRoot);
    }
    bitWriterFree(&writer);

    return headerWritten && dataWritten;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <inputDirectory> <outputArchivePath>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* inputDirectory = argv[1];
    const char* outputArchivePath = argv[2];

    FileEntry* fileEntries;
    int fileCount = listRegularFilesInDirectory(inputDirectory, &fileEntries);
    if (fileCount < 0) {
        return EXIT_FAILURE;
    }
    if (fileCount == 0) {
        fprintf(stderr, "No regular files found in '%s'\n", inputDirectory);
        return EXIT_FAILURE;
    }

    FILE* archiveFile = fopen(outputArchivePath, "wb");
    if (archiveFile == NULL) {
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

    for (int i = 0; i < fileCount; i++) {
        size_t fileSize;
        uint8_t* fileData = readEntireFile(fileEntries[i].fullPath, &fileSize);
        if (fileData == NULL) {
            printf("[%d/%d] %s -> FAILED (read error)\n", i + 1, fileCount, fileEntries[i].relativeName);
            continue;
        }

        uint64_t compressedSize = 0;
        bool wasSuccessful = compressFileIntoArchive(archiveFile,
                                                       fileEntries[i].fullPath,
                                                       fileEntries[i].relativeName,
                                                       fileData, fileSize,
                                                       &compressedSize);
        if (wasSuccessful) {
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
