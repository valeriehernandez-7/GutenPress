#define _POSIX_C_SOURCE 200809L

#include "file_scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_PATH_LENGTH 4096

int listRegularFilesInDirectory(const char* directoryPath, FileEntry** outEntries) {
    DIR* directoryStream = opendir(directoryPath);
    if (directoryStream == NULL) {
        fprintf(stderr, "Could not open directory '%s'\n", directoryPath);
        return -1;
    }

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

void freeFileEntryList(FileEntry* entries, int entryCount) {
    for (int i = 0; i < entryCount; i++) {
        free(entries[i].fullPath);
        free(entries[i].relativeName);
    }
    free(entries);
}

uint8_t* readEntireFile(const char* filePath, size_t* outFileSize) {
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
