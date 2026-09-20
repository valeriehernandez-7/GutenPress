#ifndef PROCESS_RUNNER_H
#define PROCESS_RUNNER_H

#include <stdbool.h>

// Wall-clock elapsed time is what the report's "tiempo total de corrida"
// stats need, so this is measured around the whole child process
// lifetime (fork + exec + wait), not something the child reports about
// itself — this keeps the measurement identical and fair across the
// three versions (serial, fork/IPC, pthreads), regardless of how each
// one internally measures or doesn't measure its own work.
typedef struct {
    bool   wasSuccessful;
    double elapsedSeconds;
    char   capturedOutputTail[2048];   // last portion of stdout, for error diagnostics
} ProcessResult;

// Runs executablePath with the given arguments (argv-style, NULL
// terminated after the last real argument), waits for it to finish,
// and reports how long it took. Safe to call from any thread — it does
// not touch GTK widgets itself.
ProcessResult runProcessAndMeasureTime(const char* executablePath, char* const argv[]);

#endif // PROCESS_RUNNER_H
