#ifndef WORK_PARTITION_H
#define WORK_PARTITION_H

// Returns how many workers to use for itemCount independent units of
// work: the number of available CPU cores, capped so there is never
// more workers than items (an idle worker assigned nothing would just
// waste a fork()). Falls back to 1 if the core count cannot be detected.
int detectWorkerCount(int itemCount);

// Splits itemCount items into workerCount contiguous chunks as evenly
// as possible: the first (itemCount % workerCount) workers get one
// extra item each. outStartIndices must have workerCount+1 entries;
// worker i's slice is items[outStartIndices[i] .. outStartIndices[i+1]).
void partitionItemsAcrossWorkers(int itemCount, int workerCount, int* outStartIndices);

#endif // WORK_PARTITION_H
