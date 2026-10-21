#include "MemoryBlock.h"

namespace heaptrace {

static long g_liveBlocks = 0;

MemoryBlock::MemoryBlock(MemSize start, MemSize size, bool isFree, int ownerId)
    : start_(start), size_(size), free_(isFree), ownerId_(ownerId), next_(0), prev_(0) {
    ++g_liveBlocks;
}

MemoryBlock::~MemoryBlock() { --g_liveBlocks; }

long MemoryBlock::liveCount() { return g_liveBlocks; }

}  // namespace heaptrace
