#define _POSIX_C_SOURCE 200809L

#include "process_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>

static double secondsBetween(struct timespec start, struct timespec end)
{
    double seconds = (double)(end.tv_sec - start.tv_sec);
    double nanoseconds = (double)(end.tv_nsec - start.tv_nsec);
    return seconds + (nanoseconds / 1e9);
}

// Keeps only the most recent 'tailCapacity - 1' bytes of everything
// appended so far, discarding older content as needed. Written this
// way (checking the "new chunk alone overflows" case separately)
// specifically to avoid an unsigned-integer underflow when a single
// read() returns more bytes than the whole buffer can hold — which
// happens easily here, since a 100-file run's progress log arrives in
// one or two large reads, not one line at a time.
static void appendToTailBuffer(char *tailBuffer, size_t tailCapacity,
                               const char *newData, size_t newDataLength)
{
    size_t usableCapacity = tailCapacity - 1; // reserve space for '\0'

    if (newDataLength >= usableCapacity)
    {
        memcpy(tailBuffer, newData + (newDataLength - usableCapacity), usableCapacity);
        tailBuffer[usableCapacity] = '\0';
        return;
    }

    size_t currentLength = strlen(tailBuffer);
    size_t spaceNeeded = currentLength + newDataLength;

    if (spaceNeeded > usableCapacity)
    {
        size_t dropFromFront = spaceNeeded - usableCapacity;
        memmove(tailBuffer, tailBuffer + dropFromFront, currentLength - dropFromFront);
        currentLength -= dropFromFront;
    }

    memcpy(tailBuffer + currentLength, newData, newDataLength);
    tailBuffer[currentLength + newDataLength] = '\0';
}

ProcessResult runProcessAndMeasureTime(const char *executablePath, char *const argv[])
{
    ProcessResult result;
    result.wasSuccessful = false;
    result.elapsedSeconds = 0.0;
    result.capturedOutputTail[0] = '\0';

    // The child's stdout is captured through a pipe so the GUI can show
    // a short diagnostic (e.g. the compressor's own error message) if
    // something fails, without that output ever reaching a terminal.
    int outputPipe[2];
    if (pipe(outputPipe) != 0)
    {
        return result;
    }

    struct timespec startTime, endTime;
    clock_gettime(CLOCK_MONOTONIC, &startTime);

    pid_t childPid = fork();
    if (childPid < 0)
    {
        close(outputPipe[0]);
        close(outputPipe[1]);
        return result;
    }

    if (childPid == 0)
    {
        close(outputPipe[0]);
        dup2(outputPipe[1], STDOUT_FILENO);
        dup2(outputPipe[1], STDERR_FILENO);
        close(outputPipe[1]);

        execv(executablePath, argv);
        perror("execv failed");
        _exit(EXIT_FAILURE);
    }

    close(outputPipe[1]);

    char readBuffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(outputPipe[0], readBuffer, sizeof(readBuffer))) > 0)
    {
        appendToTailBuffer(result.capturedOutputTail, sizeof(result.capturedOutputTail),
                           readBuffer, (size_t)bytesRead);
    }
    close(outputPipe[0]);

    int childStatus;
    waitpid(childPid, &childStatus, 0);

    clock_gettime(CLOCK_MONOTONIC, &endTime);
    result.elapsedSeconds = secondsBetween(startTime, endTime);
    result.wasSuccessful = WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0;

    return result;
}
