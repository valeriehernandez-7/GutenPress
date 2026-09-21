#include "file_format.h"
#include <stdio.h>
#include <string.h>

int main() {
  // --- Write a sample archive + entry header ---
  FILE *outputFile = fopen("test_archive.huff", "wb");

  ArchiveHeader archiveHeader = {.formatVersion = ARCHIVE_FORMAT_VERSION,
                                 .entryCount = 1};
  memcpy(archiveHeader.magicNumber, ARCHIVE_MAGIC_NUMBER, ARCHIVE_MAGIC_LENGTH);
  writeArchiveHeader(outputFile, &archiveHeader);

  EntryHeader entryHeader;
  const char *sampleFilename = "sample_book.txt";
  entryHeader.filenameLength = (uint16_t)strlen(sampleFilename);
  entryHeader.filename = (char *)sampleFilename;
  entryHeader.originalSize = 1024;
  entryHeader.compressedSize = 600;
  for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
    entryHeader.md5Digest[i] = (uint8_t)i;
  for (int i = 0; i < SYMBOL_ALPHABET_SIZE; i++)
    entryHeader.codeLengths[i] = 0;
  entryHeader.codeLengths['a'] = 3;
  entryHeader.paddingBits = 5;

  writeEntryHeader(outputFile, &entryHeader);
  fclose(outputFile);

  // --- Read it back ---
  FILE *inputFile = fopen("test_archive.huff", "rb");

  ArchiveHeader readArchiveHeaderResult;
  if (!readArchiveHeader(inputFile, &readArchiveHeaderResult)) {
    printf("FAILURE: could not read archive header (magic number mismatch?)\n");
    return 1;
  }

  EntryHeader readEntryHeaderResult;
  if (!readEntryHeader(inputFile, &readEntryHeaderResult)) {
    printf("FAILURE: could not read entry header\n");
    return 1;
  }
  fclose(inputFile);

  // --- Compare field by field ---
  bool allFieldsMatch = true;

  if (readArchiveHeaderResult.entryCount != archiveHeader.entryCount)
    allFieldsMatch = false;
  if (strcmp(readEntryHeaderResult.filename, sampleFilename) != 0)
    allFieldsMatch = false;
  if (readEntryHeaderResult.originalSize != entryHeader.originalSize)
    allFieldsMatch = false;
  if (readEntryHeaderResult.compressedSize != entryHeader.compressedSize)
    allFieldsMatch = false;
  if (memcmp(readEntryHeaderResult.md5Digest, entryHeader.md5Digest,
             MD5_DIGEST_LENGTH) != 0)
    allFieldsMatch = false;
  if (readEntryHeaderResult.codeLengths['a'] != 3)
    allFieldsMatch = false;
  if (readEntryHeaderResult.paddingBits != entryHeader.paddingBits)
    allFieldsMatch = false;

  printf("Filename read back: %s\n", readEntryHeaderResult.filename);
  printf("%s\n", allFieldsMatch ? "SUCCESS: all fields match after round-trip."
                                : "FAILURE: some field did not match.");

  freeEntryHeader(&readEntryHeaderResult);
  return allFieldsMatch ? 0 : 1;
}
