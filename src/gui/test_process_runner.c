#include <stdio.h>
#include "process_runner.h"

int main() {
    char* argv[] = {
        "../serial/serial_compressor",
        "../gutenberg_corpus",
        "/tmp/process_runner_test.huff",
        NULL
    };

    printf("Running serial_compressor...\n");
    ProcessResult result = runProcessAndMeasureTime("../serial/serial_compressor", argv);

    printf("Success: %s\n", result.wasSuccessful ? "yes" : "no");
    printf("Elapsed: %.4f seconds\n", result.elapsedSeconds);
    printf("--- Captured output tail ---\n%s\n", result.capturedOutputTail);

    return 0;
}
