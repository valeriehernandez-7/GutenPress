#include "work_partition.h"

#include <unistd.h>

int detectWorkerCount(int itemCount) {
    long detectedCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (detectedCores < 1) {
        detectedCores = 1;
    }

    int workerCount = (int)detectedCores;
    if (workerCount > itemCount) {
        workerCount = itemCount;
    }
    if (workerCount < 1) {
        workerCount = 1;
    }
    return workerCount;
}

void partitionItemsAcrossWorkers(int itemCount, int workerCount, int* outStartIndices) {
    int baseChunkSize = itemCount / workerCount;
    int remainder = itemCount % workerCount;

    outStartIndices[0] = 0;
    for (int i = 0; i < workerCount; i++) {
        int chunkSize = baseChunkSize + (i < remainder ? 1 : 0);
        outStartIndices[i + 1] = outStartIndices[i] + chunkSize;
    }
}
