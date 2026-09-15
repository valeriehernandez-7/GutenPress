#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

#include "../common/file_format.h"
#include "../common/md5_utils.h"
#include "../common/huffman.h"
#include "../common/bit_stream.h"
#include "../common/work_partition.h"
#include "../common/directory_utils.h"

#define MAX_PATH_LENGTH 4096

// Everything the parent needs to know about one archive entry before
// any worker starts: where its compressed payload sits in the file
// (byteOffset), so each worker can fseek() straight to it without
// reading every entry before it.
typedef struct {
    char*    filename;
    uint64_t originalSize;
    uint64_t compressedSize;
    uint8_t  md5Digest[MD5_DIGEST_LENGTH];
    uint8_t  codeLengths[SYMBOL_ALPHABET_SIZE];
    long     byteOffset;   // position of this entry's compressed data in the archive
} EntryLocation;

// A worker's result for one file is fixed-size and small (unlike the
// compressor's variable-length payloads), so it can be sent over the
// pipe as a single flat struct with no separate header/payload framing.
typedef struct {
    bool wasSuccessful;
    bool md5Matched;
} WorkerResult;


// Reads every EntryHeader up front (cheap: at most a few hundred bytes
// each) and records where each entry's compressed payload starts, so
// workers can jump directly to their assigned entries later instead of
// re-parsing headers they do not need.
static int readAllEntryLocations(const char* archivePath, EntryLocation** outLocations) {
    FILE* archiveFile = fopen(archivePath, "rb");
    if (archiveFile == NULL) {
        fprintf(stderr, "Could not open archive '%s'\n", archivePath);
        return -1;
    }

    ArchiveHeader archiveHeader;
    if (!readArchiveHeader(archiveFile, &archiveHeader)) {
        fprintf(stderr, "'%s' is not a valid archive produced by this program\n", archivePath);
        fclose(archiveFile);
        return -1;
    }

    EntryLocation* locations = malloc(archiveHeader.entryCount * sizeof(EntryLocation));
    if (locations == NULL) {
        fprintf(stderr, "Out of memory while reading archive index\n");
        fclose(archiveFile);
        return -1;
    }

    for (uint32_t i = 0; i < archiveHeader.entryCount; i++) {
        EntryHeader entryHeader;
        if (!readEntryHeader(archiveFile, &entryHeader)) {
            fprintf(stderr, "Archive appears truncated after %u entries\n", i);
            free(locations);
            fclose(archiveFile);
            return -1;
        }

        locations[i].filename = strdup(entryHeader.filename);
        locations[i].originalSize = entryHeader.originalSize;
        locations[i].compressedSize = entryHeader.compressedSize;
        memcpy(locations[i].md5Digest, entryHeader.md5Digest, MD5_DIGEST_LENGTH);
        memcpy(locations[i].codeLengths, entryHeader.codeLengths, SYMBOL_ALPHABET_SIZE);

        locations[i].byteOffset = ftell(archiveFile);
        fseek(archiveFile, (long)entryHeader.compressedSize, SEEK_CUR);

        freeEntryHeader(&entryHeader);
    }

    fclose(archiveFile);
    *outLocations = locations;
    return (int)archiveHeader.entryCount;
}

static void freeEntryLocationList(EntryLocation* locations, int count) {
    for (int i = 0; i < count; i++) {
        free(locations[i].filename);
    }
    free(locations);
}

// Runs entirely inside a forked child: decompresses and verifies its
// assigned slice of entries, writing each result to writeFd as a flat
// WorkerResult struct — no variable-length framing needed, since every
// record is exactly sizeof(WorkerResult) bytes.
static void runDecompressorWorker(int writeFd, const char* archivePath, const char* outputDirectory,
                                   const EntryLocation* assignedEntries, int assignedCount) {
    FILE* archiveFile = fopen(archivePath, "rb");
    if (archiveFile == NULL) {
        _exit(EXIT_FAILURE);
    }

    for (int i = 0; i < assignedCount; i++) {
        const EntryLocation* location = &assignedEntries[i];
        WorkerResult result = { .wasSuccessful = false, .md5Matched = false };

        fseek(archiveFile, location->byteOffset, SEEK_SET);

        uint8_t* compressedBuffer = NULL;
        if (location->compressedSize > 0) {
            compressedBuffer = malloc(location->compressedSize);
            if (compressedBuffer == NULL ||
                fread(compressedBuffer, 1, location->compressedSize, archiveFile) != location->compressedSize) {
                free(compressedBuffer);
                write(writeFd, &result, sizeof(result));
                continue;
            }
        }

        uint8_t* decodedBuffer = NULL;
        if (location->originalSize > 0) {
            decodedBuffer = malloc(location->originalSize);
            if (decodedBuffer == NULL) {
                free(compressedBuffer);
                write(writeFd, &result, sizeof(result));
                continue;
            }

            BitReader reader;
            bitReaderInit(&reader, compressedBuffer, location->compressedSize);
            decodeBuffer(&reader, location->codeLengths, location->originalSize, decodedBuffer);
        }

        char outputFilePath[MAX_PATH_LENGTH];
        snprintf(outputFilePath, sizeof(outputFilePath), "%s/%s", outputDirectory, location->filename);

        FILE* outputFile = fopen(outputFilePath, "wb");
        bool writeSucceeded = false;
        if (outputFile != NULL) {
            writeSucceeded = (location->originalSize == 0) ||
                (fwrite(decodedBuffer, 1, location->originalSize, outputFile) == location->originalSize);
            fclose(outputFile);
        }

        free(compressedBuffer);
        free(decodedBuffer);

        if (writeSucceeded) {
            uint8_t recomputedDigest[MD5_DIGEST_LENGTH];
            if (computeFileMd5(outputFilePath, recomputedDigest)) {
                result.wasSuccessful = true;
                result.md5Matched = digestsAreEqual(recomputedDigest, location->md5Digest);
            }
        }

        write(writeFd, &result, sizeof(result));
    }

    fclose(archiveFile);
}

// Same fork-all-first-then-poll() strategy as the compressor, for the
// same reason: forking workers one at a time and reading each to
// completion before forking the next would serialize them, defeating
// the point of parallelism. Unlike the compressor, WorkerResult
// records are fixed-size, so poll()-driven reads can never split a
// record mid-way — no per-worker growable buffer is needed here.
static bool runAllWorkersAndCollectResults(const char* archivePath, const char* outputDirectory,
                                            const EntryLocation* locations, const int* startIndices,
                                            int workerCount, int* outVerifiedCount, int* outExtractedCount) {
    int pipeFds[workerCount][2];
    pid_t childPids[workerCount];

    for (int i = 0; i < workerCount; i++) {
        if (pipe(pipeFds[i]) != 0) {
            fprintf(stderr, "Failed to create pipe for worker %d\n", i);
            return false;
        }
    }

    for (int i = 0; i < workerCount; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            fprintf(stderr, "Failed to fork worker %d\n", i);
            return false;
        }

        if (pid == 0) {
            for (int j = 0; j < workerCount; j++) {
                if (j == i) {
                    close(pipeFds[j][0]);
                } else {
                    close(pipeFds[j][0]);
                    close(pipeFds[j][1]);
                }
            }

            int assignedStart = startIndices[i];
            int assignedCount = startIndices[i + 1] - startIndices[i];
            runDecompressorWorker(pipeFds[i][1], archivePath, outputDirectory,
                                   &locations[assignedStart], assignedCount);
            close(pipeFds[i][1]);
            _exit(EXIT_SUCCESS);
        }

        childPids[i] = pid;
    }

    for (int i = 0; i < workerCount; i++) {
        close(pipeFds[i][1]);
    }

    struct pollfd pollDescriptors[workerCount];
    for (int i = 0; i < workerCount; i++) {
        pollDescriptors[i].fd = pipeFds[i][0];
        pollDescriptors[i].events = POLLIN;
    }

    int remainingOpenPipes = workerCount;
    *outVerifiedCount = 0;
    *outExtractedCount = 0;

    while (remainingOpenPipes > 0) {
        int readyCount = poll(pollDescriptors, workerCount, -1);
        if (readyCount < 0) {
            fprintf(stderr, "poll() failed while collecting worker results\n");
            return false;
        }

        for (int i = 0; i < workerCount; i++) {
            if (pollDescriptors[i].fd == -1) {
                continue;
            }
            if ((pollDescriptors[i].revents & (POLLIN | POLLHUP)) == 0) {
                continue;
            }

            WorkerResult result;
            ssize_t bytesRead = read(pipeFds[i][0], &result, sizeof(result));

            if (bytesRead == sizeof(result)) {
                if (result.wasSuccessful) {
                    (*outExtractedCount)++;
                    if (result.md5Matched) {
                        (*outVerifiedCount)++;
                    }
                }
            } else {
                close(pipeFds[i][0]);
                pollDescriptors[i].fd = -1;
                remainingOpenPipes--;
            }
        }
    }

    bool allWorkersSucceeded = true;
    for (int i = 0; i < workerCount; i++) {
        int workerStatus;
        waitpid(childPids[i], &workerStatus, 0);
        if (!WIFEXITED(workerStatus) || WEXITSTATUS(workerStatus) != 0) {
            fprintf(stderr, "Worker %d exited abnormally\n", i);
            allWorkersSucceeded = false;
        }
    }

    return allWorkersSucceeded;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <inputArchivePath> <outputDirectory>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* inputArchivePath = argv[1];
    const char* outputDirectory = argv[2];

    EntryLocation* locations;
    int entryCount = readAllEntryLocations(inputArchivePath, &locations);
    if (entryCount <= 0) {
        return EXIT_FAILURE;
    }

    if (!ensureDirectoryExists(outputDirectory)) {
        freeEntryLocationList(locations, entryCount);
        return EXIT_FAILURE;
    }

    int workerCount = detectWorkerCount(entryCount);
    int startIndices[workerCount + 1];
    partitionItemsAcrossWorkers(entryCount, workerCount, startIndices);

    int verifiedCount = 0;
    int extractedCount = 0;
    bool allWorkersSucceeded = runAllWorkersAndCollectResults(inputArchivePath, outputDirectory,
                                                                locations, startIndices, workerCount,
                                                                &verifiedCount, &extractedCount);

    freeEntryLocationList(locations, entryCount);

    double healthPercentage = (entryCount > 0)
        ? (100.0 * (double)verifiedCount / (double)entryCount)
        : 0.0;

    printf("\n--- Summary ---\n");
    printf("Workers used: %d\n", workerCount);
    printf("Files extracted: %d/%d\n", extractedCount, entryCount);
    printf("MD5 verified: %d/%d\n", verifiedCount, entryCount);
    printf("Compression health: %.2f%%\n", healthPercentage);

    return (allWorkersSucceeded && verifiedCount == entryCount) ? EXIT_SUCCESS : EXIT_FAILURE;
}
