#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/compression_pipeline.h"
#include "../common/file_format.h"
#include "../common/file_scanner.h"
#include "../common/growable_buffer.h"
#include "../common/work_partition.h"

// Guards processedFileCount, the one piece of state every thread
// writes to. Everything else a thread touches (its own slice of
// fileEntries, its own GrowableBuffer) is exclusively its own, so no
// other shared state needs protection here.
static pthread_mutex_t progressMutex = PTHREAD_MUTEX_INITIALIZER;
static int processedFileCount = 0;
static int totalFileCount = 0;

// Everything one thread needs to do its slice of work and report back,
// bundled together since pthread_create only passes a single void*.
typedef struct {
  const FileEntry *assignedFiles;
  int assignedFileCount;
  GrowableBuffer
      *outputBuffer; // this thread's own buffer — no mutex needed to write here
} WorkerArgs;

static void *runCompressorWorker(void *argsPointer) {
  WorkerArgs *args = (WorkerArgs *)argsPointer;
  growableBufferInit(args->outputBuffer);

  for (int i = 0; i < args->assignedFileCount; i++) {
    const FileEntry *entry = &args->assignedFiles[i];

    size_t fileSize;
    uint8_t *fileData = readEntireFile(entry->fullPath, &fileSize);
    if (fileData == NULL) {
      fprintf(stderr, "[thread %lu] failed to read '%s'\n",
              (unsigned long)pthread_self(), entry->fullPath);
      continue;
    }

    // compressFileIntoStream writes through a FILE*, so the
    // thread's in-memory buffer is wrapped with open_memstream —
    // this lets every version (serial, fork/IPC, pthreads) reuse
    // the exact same compression function regardless of where its
    // output ultimately ends up.
    char *memoryStreamPointer;
    size_t memoryStreamSize;
    FILE *memoryStream =
        open_memstream(&memoryStreamPointer, &memoryStreamSize);

    uint64_t compressedSize;
    bool wasSuccessful = compressFileIntoStream(memoryStream, entry->fullPath,
                                                entry->relativeName, fileData,
                                                fileSize, &compressedSize);
    fclose(memoryStream); // flushes memoryStreamPointer/memoryStreamSize

    if (wasSuccessful) {
      growableBufferAppend(args->outputBuffer,
                           (const uint8_t *)memoryStreamPointer,
                           memoryStreamSize);
    } else {
      fprintf(stderr, "[thread %lu] failed to compress '%s'\n",
              (unsigned long)pthread_self(), entry->relativeName);
    }

    free(memoryStreamPointer);
    free(fileData);

    pthread_mutex_lock(&progressMutex);
    processedFileCount++;
    printf("[%d/%d] %s -> %s\n", processedFileCount, totalFileCount,
           entry->relativeName, wasSuccessful ? "OK" : "FAILED");
    pthread_mutex_unlock(&progressMutex);
  }

  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <inputDirectory> <outputArchivePath>\n",
            argv[0]);
    return EXIT_FAILURE;
  }

  const char *inputDirectory = argv[1];
  const char *outputArchivePath = argv[2];

  FileEntry *fileEntries;
  int fileCount = listRegularFilesInDirectory(inputDirectory, &fileEntries);
  if (fileCount <= 0) {
    fprintf(stderr, "No regular files found in '%s'\n", inputDirectory);
    return EXIT_FAILURE;
  }
  totalFileCount = fileCount;

  int workerCount = detectWorkerCount(fileCount);
  int startIndices[workerCount + 1];
  partitionItemsAcrossWorkers(fileCount, workerCount, startIndices);

  pthread_t threads[workerCount];
  WorkerArgs threadArgs[workerCount];
  GrowableBuffer threadBuffers[workerCount];

  for (int i = 0; i < workerCount; i++) {
    threadArgs[i].assignedFiles = &fileEntries[startIndices[i]];
    threadArgs[i].assignedFileCount = startIndices[i + 1] - startIndices[i];
    threadArgs[i].outputBuffer = &threadBuffers[i];

    if (pthread_create(&threads[i], NULL, runCompressorWorker,
                       &threadArgs[i]) != 0) {
      fprintf(stderr, "Failed to create thread %d\n", i);
      return EXIT_FAILURE;
    }
  }

  for (int i = 0; i < workerCount; i++) {
    pthread_join(threads[i], NULL);
  }

  FILE *archiveFile = fopen(outputArchivePath, "wb");
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
    fwrite(threadBuffers[i].data, 1, threadBuffers[i].length, archiveFile);
    growableBufferFree(&threadBuffers[i]);
  }

  fclose(archiveFile);
  freeFileEntryList(fileEntries, fileCount);

  // Re-reads the archive just written to compute totals, the same
  // approach fork_compressor uses — the archive itself is the single
  // source of truth, rather than tracking sizes through thread state.
  archiveFile = fopen(outputArchivePath, "rb");
  ArchiveHeader readBackHeader;
  readArchiveHeader(archiveFile, &readBackHeader);

  uint64_t totalOriginalSize = 0;
  uint64_t totalCompressedSize = 0;
  uint32_t actualEntryCount = 0;

  while (actualEntryCount < readBackHeader.entryCount) {
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

  double compressionRatio =
      (totalOriginalSize > 0)
          ? (double)totalCompressedSize / (double)totalOriginalSize
          : 0.0;

  printf("\n--- Summary ---\n");
  printf("Threads used: %d\n", workerCount);
  printf("Files compressed: %u/%d\n", actualEntryCount, fileCount);
  printf("Total original size: %llu bytes\n",
         (unsigned long long)totalOriginalSize);
  printf("Total compressed size: %llu bytes\n",
         (unsigned long long)totalCompressedSize);
  printf("Compression ratio: %.4f\n", compressionRatio);

  return EXIT_SUCCESS;
}
