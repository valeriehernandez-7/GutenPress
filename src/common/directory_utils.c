#include "directory_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_PATH_LENGTH 4096

bool ensureDirectoryExists(const char *directoryPath) {
  char pathBuffer[MAX_PATH_LENGTH];
  snprintf(pathBuffer, sizeof(pathBuffer), "%s", directoryPath);

  for (char *cursor = pathBuffer + 1; *cursor != '\0'; cursor++) {
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
