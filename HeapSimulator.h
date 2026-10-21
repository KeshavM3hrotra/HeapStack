// HeapSimulator.h - the allocation engine: owns the linked list of MemoryBlocks.
//
//  * allocate(): asks the active AllocationStrategy for a free block, splits it
//    when it is larger than the request.
//  * release():  frees a VM's block and merges it with free neighbours.
//  * The list always covers [0, totalSize) with no gaps or overlaps, and no two
//    free blocks are ever adjacent (validate() checks all of this).
#ifndef HEAPTRACE_HEAPSIMULATOR_H
#define HEAPTRACE_HEAPSIMULATOR_H

#include "AllocationStrategy.h"
#include "MemoryBlock.h"

#include <memory>
#include <string>

namespace heaptrace {

enum class RejectReason {
    None,
    Fragmentation,       // enough memory is free in total, but no single block is large enough
    InsufficientMemory,  // less memory is free in total than requested
    InvalidRequest       // bad owner id / size, or the owner already holds a block
};

struct AllocationResult {
    bool success;
    MemSize start;  // start address of the placed block, -1 if rejected
    RejectReason reason;
    AllocationResult() : success(false), start(-1), reason(RejectReason::InvalidRequest) {}
};

struct ReleaseResult {
    bool found;
    MemSize start;  // start address of the released block
    MemSize size;   // size of the released block
    int merges;     // how many free neighbours it was merged with (0, 1 or 2)
    ReleaseResult() : found(false), start(-1), size(0), merges(0) {}
};

struct HeapStats {
    MemSize total;
    MemSize used;
    MemSize freeMemory;
    int usedBlocks;
    int freeBlocks;
    MemSize largestFree;
    // 1 - largestFree / freeMemory; defined as 0 when nothing is free.
    double externalFragmentation;
    HeapStats()
        : total(0), used(0), freeMemory(0), usedBlocks(0), freeBlocks(0), largestFree(0),
          externalFragmentation(0.0) {}
};

class HeapSimulator {
public:
    // Throws std::invalid_argument if totalSize is not in [1, MAX_MEMORY_MB].
    explicit HeapSimulator(MemSize totalSize);
    ~HeapSimulator();

    HeapSimulator(const HeapSimulator&) = delete;
    HeapSimulator& operator=(const HeapSimulator&) = delete;

    // Replaces the active policy (a null pointer is ignored).
    void setStrategy(std::unique_ptr<AllocationStrategy> strategy);
    const AllocationStrategy& strategy() const { return *strategy_; }

    AllocationResult allocate(int ownerId, MemSize size);
    ReleaseResult release(int ownerId);

    HeapStats stats() const;
    MemSize totalSize() const { return total_; }
    const MemoryBlock* head() const { return head_; }

    // Block that contains address `pos` (0 <= pos < totalSize), or null.
    const MemoryBlock* blockAt(MemSize pos) const;

    // Checks every structural invariant of the list. On failure returns false
    // and, if `error` is non-null, stores a description.
    bool validate(std::string* error = 0) const;

private:
    bool ownsBlock(int ownerId) const;

    MemSize total_;
    MemoryBlock* head_;
    std::unique_ptr<AllocationStrategy> strategy_;
};

}  // namespace heaptrace

#endif
