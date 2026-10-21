// MemoryBlock.h - one node of the hand-written doubly linked heap list.
//
// A block describes one contiguous region [start, start + size) of the
// simulated server memory: either free, or owned by a VM (ownerId).
// Only HeapSimulator may create, link, resize or delete blocks.
#ifndef HEAPTRACE_MEMORYBLOCK_H
#define HEAPTRACE_MEMORYBLOCK_H

#include "Types.h"

namespace heaptrace {

class MemoryBlock {
public:
    MemSize start() const { return start_; }
    MemSize size() const { return size_; }
    MemSize end() const { return start_ + size_; }
    bool isFree() const { return free_; }
    int ownerId() const { return ownerId_; }
    MemoryBlock* next() const { return next_; }
    MemoryBlock* prev() const { return prev_; }

    // Number of MemoryBlock objects currently alive (used by the tests to prove
    // that the destructor of HeapSimulator releases every node).
    static long liveCount();

private:
    friend class HeapSimulator;

    MemoryBlock(MemSize start, MemSize size, bool isFree, int ownerId);
    ~MemoryBlock();
    MemoryBlock(const MemoryBlock&) = delete;
    MemoryBlock& operator=(const MemoryBlock&) = delete;

    MemSize start_;
    MemSize size_;
    bool free_;
    int ownerId_;
    MemoryBlock* next_;
    MemoryBlock* prev_;
};

}  // namespace heaptrace

#endif
