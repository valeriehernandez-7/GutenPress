#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>

#include "../common/file_format.h"
#include "../common/file_scanner.h"
#include "../common/compression_pipeline.h"
#include "../common/work_partition.h"
#include "pipe_buffer.h"

// Detected once in main() and used for the lifetime of the program, so
// every function that needs the worker count reads the same value
// instead of re-querying sysconf() or passing it through every call.
static int workerCount = 0;

// Runs entirely inside a forked child: compresses its assigned slice
// of files and writes their EntryHeader + payload records to writeFd,
// reusing the exact same per-file logic the serial version uses.
static void runCompressorWorker(int writeFd, const FileEntry* assignedFiles, int assignedFileCount) {
    FILE* outputStream = fdopen(writeFd, "wb");
    if (outputStream == NULL) {
        _exit(EXIT_FAILURE);
    }

    for (int i = 0; i < assignedFileCount; i++) {
        size_t fileSize;
        uint8_t* fileData = readEntireFile(assignedFiles[i].fullPath, &fileSize);
        if (fileData == NULL) {
            fprintf(stderr, "[worker pid %d] failed to read '%s'\n",
                    getpid(), assignedFiles[i].fullPath);
            continue;
        }

        uint64_t compressedSize;
        bool wasSuccessful = compressFileIntoStream(outputStream,
                                                      assignedFiles[i].fullPath,
                                                      assignedFiles[i].relativeName,
                                                      fileData, fileSize,
                                                      &compressedSize);
        if (!wasSuccessful) {
            fprintf(stderr, "[worker pid %d] failed to compress '%s'\n",
                    getpid(), assignedFiles[i].relativeName);
        }

        free(fileData);
    }

    fclose(outputStream);   // also closes writeFd
}

// Forks all workers up front (rather than one at a time) so they run
// truly in parallel, then uses poll() to drain whichever pipe has data
// ready. Reading pipes sequentially instead would risk a deadlock: a
// worker's pipe buffer is finite (tens of KB), and this corpus's
// compressed output per worker (megabytes) far exceeds it — a worker
// would block on write() while the parent was stuck reading a
// different, slower worker's pipe.
static bool runAllWorkersAndCollectOutput(const FileEntry* fileEntries, const int* startIndices,
                                           PipeBuffer* workerBuffers) {
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
            // Child inherited every pipe's file descriptors, not just
            // its own — every end that isn't this worker's write end
            // must be closed, or a stray copy of a read end left open
            // in some child would stop the parent from ever seeing
            // EOF on that pipe (EOF only fires once ALL write ends,
            // across every process, are closed).
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
            runCompressorWorker(pipeFds[i][1], &fileEntries[assignedStart], assignedCount);
            _exit(EXIT_SUCCESS);
        }

        childPids[i] = pid;
    }

    // Parent never writes to any pipe, and an open write end here
    // would itself prevent EOF from ever being observed on reads.
    for (int i = 0; i < workerCount; i++) {
        close(pipeFds[i][1]);
    }

    struct pollfd pollDescriptors[workerCount];
    for (int i = 0; i < workerCount; i++) {
        pollDescriptors[i].fd = pipeFds[i][0];
        pollDescriptors[i].events = POLLIN;
    }

    int remainingOpenPipes = workerCount;
    uint8_t readChunk[65536];

    while (remainingOpenPipes > 0) {
        int readyCount = poll(pollDescriptors, workerCount, -1);
        if (readyCount < 0) {
            fprintf(stderr, "poll() failed while collecting worker output\n");
            return false;
        }

        for (int i = 0; i < workerCount; i++) {
            if (pollDescriptors[i].fd == -1) {
                continue;   // already finished, excluded from polling
            }
            if ((pollDescriptors[i].revents & (POLLIN | POLLHUP)) == 0) {
                continue;
            }

            ssize_t bytesRead = read(pipeFds[i][0], readChunk, sizeof(readChunk));
            if (bytesRead > 0) {
                pipeBufferAppend(&workerBuffers[i], readChunk, (size_t)bytesRead);
            } else {
                // 0 means EOF (worker closed its write end); negative
                // means a real read error — either way, this pipe is done.
                close(pipeFds[i][0]);
                pollDescriptors[i].fd = -1;   // poll() ignores negative fds
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

// Re-reads the archive this program just wrote to compute summary
// statistics, reusing readArchiveHeader/readEntryHeader instead of
// tracking sizes through the pipe protocol — the archive is the single
// source of truth for what actually got written.
static void printArchiveSummary(const char* archivePath, int expectedEntryCount) {
    FILE* archiveFile = fopen(archivePath, "rb");
    if (archiveFile == NULL) {
        fprintf(stderr, "Could not reopen '%s' to compute summary\n", archivePath);
        return;
    }

    ArchiveHeader archiveHeader;
    if (!readArchiveHeader(archiveFile, &archiveHeader)) {
        fprintf(stderr, "Could not read back archive header for summary\n");
        fclose(archiveFile);
        return;
    }

    uint64_t totalOriginalSize = 0;
    uint64_t totalCompressedSize = 0;
    uint32_t actualEntryCount = 0;

    while (actualEntryCount < archiveHeader.entryCount) {
        EntryHeader entryHeader;
        if (!readEntryHeader(archiveFile, &entryHeader)) {
            break;
        }

        totalOriginalSize += entryHeader.originalSize;
        totalCompressedSize += entryHeader.compressedSize;
        actualEntryCount++;

        fseek(archiveFile, (long)entryHeader.compressedSize, SEEK_CUR);
        freeEntryHeader(&entryHeader);
    }

    fclose(archiveFile);

    double compressionRatio = (totalOriginalSize > 0)
        ? (double)totalCompressedSize / (double)totalOriginalSize
        : 0.0;

    printf("\n--- Summary ---\n");
    printf("Workers used: %d\n", workerCount);
    printf("Files compressed: %u/%d\n", actualEntryCount, expectedEntryCount);
    printf("Total original size: %llu bytes\n", (unsigned long long)totalOriginalSize);
    printf("Total compressed size: %llu bytes\n", (unsigned long long)totalCompressedSize);
    printf("Compression ratio: %.4f\n", compressionRatio);
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
    if (fileCount <= 0) {
        fprintf(stderr, "No regular files found in '%s'\n", inputDirectory);
        return EXIT_FAILURE;
    }

    workerCount = detectWorkerCount(fileCount);

    int startIndices[workerCount + 1];
    partitionItemsAcrossWorkers(fileCount, workerCount, startIndices);

    PipeBuffer workerBuffers[workerCount];
    for (int i = 0; i < workerCount; i++) {
        pipeBufferInit(&workerBuffers[i]);
    }

    bool allWorkersSucceeded = runAllWorkersAndCollectOutput(fileEntries, startIndices, workerBuffers);

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

    for (int i = 0; i < workerCount; i++) {
        fwrite(workerBuffers[i].data, 1, workerBuffers[i].length, archiveFile);
        pipeBufferFree(&workerBuffers[i]);
    }

    fclose(archiveFile);
    freeFileEntryList(fileEntries, fileCount);

    printArchiveSummary(outputArchivePath, fileCount);

    return allWorkersSucceeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
