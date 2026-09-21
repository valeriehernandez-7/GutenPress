#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/bit_stream.h"
#include "../common/directory_utils.h"
#include "../common/file_format.h"
#include "../common/huffman.h"
#include "../common/md5_utils.h"
#include "../common/work_partition.h"

#define MAX_PATH_LENGTH 4096

// Everything the main thread needs to know about one archive entry
// before any worker thread starts: where its compressed payload sits
// in the file (byteOffset), so each thread can fseek() straight to it
// on its own FILE* handle without reading every entry before it.
typedef struct {
  char *filename;
  uint64_t originalSize;
  uint64_t compressedSize;
  uint8_t md5Digest[MD5_DIGEST_LENGTH];
  uint8_t codeLengths[SYMBOL_ALPHABET_SIZE];
  long byteOffset;
} EntryLocation;

// Guards the two counters every thread updates. Everything else a
// thread touches (its own slice of entries, its own FILE* handle on
// the archive) is exclusively its own, so no other shared state needs
// protection here.
static pthread_mutex_t resultsMutex = PTHREAD_MUTEX_INITIALIZER;
static int verifiedCount = 0;
static int extractedCount = 0;

typedef struct {
  const char *archivePath;
  const char *outputDirectory;
  const EntryLocation *assignedEntries;
  int assignedCount;
} WorkerArgs;

static int readAllEntryLocations(const char *archivePath,
                                 EntryLocation **outLocations) {
  FILE *archiveFile = fopen(archivePath, "rb");
  if (archiveFile == NULL) {
    fprintf(stderr, "Could not open archive '%s'\n", archivePath);
    return -1;
  }

  ArchiveHeader archiveHeader;
  if (!readArchiveHeader(archiveFile, &archiveHeader)) {
    fprintf(stderr, "'%s' is not a valid archive produced by this program\n",
            archivePath);
    fclose(archiveFile);
    return -1;
  }

  EntryLocation *locations =
      malloc(archiveHeader.entryCount * sizeof(EntryLocation));
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
    memcpy(locations[i].codeLengths, entryHeader.codeLengths,
           SYMBOL_ALPHABET_SIZE);

    locations[i].byteOffset = ftell(archiveFile);
    fseek(archiveFile, (long)entryHeader.compressedSize, SEEK_CUR);

    freeEntryHeader(&entryHeader);
  }

  fclose(archiveFile);
  *outLocations = locations;
  return (int)archiveHeader.entryCount;
}

static void freeEntryLocationList(EntryLocation *locations, int count) {
  for (int i = 0; i < count; i++) {
    free(locations[i].filename);
  }
  free(locations);
}

static void *runDecompressorWorker(void *argsPointer) {
  WorkerArgs *args = (WorkerArgs *)argsPointer;

  // Each thread opens its own FILE* on the archive, since FILE*'s
  // internal read position is not safe to share across threads —
  // two threads seeking and reading through the same handle would
  // race and corrupt each other's reads.
  FILE *archiveFile = fopen(args->archivePath, "rb");
  if (archiveFile == NULL) {
    return NULL;
  }

  for (int i = 0; i < args->assignedCount; i++) {
    const EntryLocation *location = &args->assignedEntries[i];
    bool wasSuccessful = false;
    bool md5Matched = false;

    fseek(archiveFile, location->byteOffset, SEEK_SET);

    uint8_t *compressedBuffer = NULL;
    bool readOk = true;
    if (location->compressedSize > 0) {
      compressedBuffer = malloc(location->compressedSize);
      if (compressedBuffer == NULL ||
          fread(compressedBuffer, 1, location->compressedSize, archiveFile) !=
              location->compressedSize) {
        readOk = false;
      }
    }

    uint8_t *decodedBuffer = NULL;
    if (readOk && location->originalSize > 0) {
      decodedBuffer = malloc(location->originalSize);
      if (decodedBuffer != NULL) {
        BitReader reader;
        bitReaderInit(&reader, compressedBuffer, location->compressedSize);
        decodeBuffer(&reader, location->codeLengths, location->originalSize,
                     decodedBuffer);
      } else {
        readOk = false;
      }
    }

    if (readOk) {
      char outputFilePath[MAX_PATH_LENGTH];
      snprintf(outputFilePath, sizeof(outputFilePath), "%s/%s",
               args->outputDirectory, location->filename);

      FILE *outputFile = fopen(outputFilePath, "wb");
      bool writeSucceeded = false;
      if (outputFile != NULL) {
        writeSucceeded = (location->originalSize == 0) ||
                         (fwrite(decodedBuffer, 1, location->originalSize,
                                 outputFile) == location->originalSize);
        fclose(outputFile);
      }

      if (writeSucceeded) {
        uint8_t recomputedDigest[MD5_DIGEST_LENGTH];
        if (computeFileMd5(outputFilePath, recomputedDigest)) {
          wasSuccessful = true;
          md5Matched = digestsAreEqual(recomputedDigest, location->md5Digest);
        }
      }
    }

    free(compressedBuffer);
    free(decodedBuffer);

    pthread_mutex_lock(&resultsMutex);
    if (wasSuccessful) {
      extractedCount++;
      if (md5Matched) {
        verifiedCount++;
      }
    }
    printf("[thread %lu] %s -> %s (MD5 %s)\n", (unsigned long)pthread_self(),
           location->filename, wasSuccessful ? "OK" : "FAILED",
           md5Matched ? "verified" : "MISMATCH");
    pthread_mutex_unlock(&resultsMutex);
  }

  fclose(archiveFile);
  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <inputArchivePath> <outputDirectory>\n",
            argv[0]);
    return EXIT_FAILURE;
  }

  const char *inputArchivePath = argv[1];
  const char *outputDirectory = argv[2];

  EntryLocation *locations;
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

  pthread_t threads[workerCount];
  WorkerArgs threadArgs[workerCount];

  for (int i = 0; i < workerCount; i++) {
    threadArgs[i].archivePath = inputArchivePath;
    threadArgs[i].outputDirectory = outputDirectory;
    threadArgs[i].assignedEntries = &locations[startIndices[i]];
    threadArgs[i].assignedCount = startIndices[i + 1] - startIndices[i];

    if (pthread_create(&threads[i], NULL, runDecompressorWorker,
                       &threadArgs[i]) != 0) {
      fprintf(stderr, "Failed to create thread %d\n", i);
      return EXIT_FAILURE;
    }
  }

  for (int i = 0; i < workerCount; i++) {
    pthread_join(threads[i], NULL);
  }

  freeEntryLocationList(locations, entryCount);

  double healthPercentage =
      (entryCount > 0) ? (100.0 * (double)verifiedCount / (double)entryCount)
                       : 0.0;

  printf("\n--- Summary ---\n");
  printf("Threads used: %d\n", workerCount);
  printf("Files extracted: %d/%d\n", extractedCount, entryCount);
  printf("MD5 verified: %d/%d\n", verifiedCount, entryCount);
  printf("Compression health: %.2f%%\n", healthPercentage);

  return (verifiedCount == entryCount) ? EXIT_SUCCESS : EXIT_FAILURE;
}
