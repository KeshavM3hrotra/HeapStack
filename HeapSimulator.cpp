#include "HeapSimulator.h"

#include "Util.h"

#include <stdexcept>

namespace heaptrace {

HeapSimulator::HeapSimulator(MemSize totalSize)
    : total_(totalSize), head_(0), strategy_(new FirstFitStrategy()) {
    if (totalSize < 1 || totalSize > MAX_MEMORY_MB) {
        throw std::invalid_argument("heap size must be between 1 and 1000000000000 MB");
    }
    head_ = new MemoryBlock(0, totalSize, true, NO_OWNER);
}

HeapSimulator::~HeapSimulator() {
    MemoryBlock* b = head_;
    while (b != 0) {
        MemoryBlock* nxt = b->next_;
        delete b;
        b = nxt;
    }
    head_ = 0;
}

void HeapSimulator::setStrategy(std::unique_ptr<AllocationStrategy> strategy) {
    if (strategy) strategy_ = std::move(strategy);
}

bool HeapSimulator::ownsBlock(int ownerId) const {
    for (const MemoryBlock* b = head_; b != 0; b = b->next_) {
        if (!b->free_ && b->ownerId_ == ownerId) return true;
    }
    return false;
}

AllocationResult HeapSimulator::allocate(int ownerId, MemSize size) {
    AllocationResult result;  // defaults to InvalidRequest
    if (ownerId < 0 || size < 1 || size > MAX_MEMORY_MB || ownsBlock(ownerId)) return result;

    MemoryBlock* chosen = strategy_->selectBlock(head_, size);
    // Defensive: never trust a strategy blindly.
    if (chosen == 0 || !chosen->free_ || chosen->size_ < size) {
        result.reason = (stats().freeMemory >= size) ? RejectReason::Fragmentation
                                                     : RejectReason::InsufficientMemory;
        return result;
    }

    if (chosen->size_ > size) {
        // Split: the front part serves the request, the tail stays free.
        MemoryBlock* rest = new MemoryBlock(chosen->start_ + size, chosen->size_ - size, true, NO_OWNER);
        rest->prev_ = chosen;
        rest->next_ = chosen->next_;
        if (chosen->next_ != 0) chosen->next_->prev_ = rest;
        chosen->next_ = rest;
        chosen->size_ = size;
    }
    chosen->free_ = false;
    chosen->ownerId_ = ownerId;

    result.success = true;
    result.start = chosen->start_;
    result.reason = RejectReason::None;
    return result;
}

ReleaseResult HeapSimulator::release(int ownerId) {
    ReleaseResult result;
    if (ownerId < 0) return result;

    MemoryBlock* b = head_;
    while (b != 0 && (b->free_ || b->ownerId_ != ownerId)) b = b->next_;
    if (b == 0) return result;

    result.found = true;
    result.start = b->start_;
    result.size = b->size_;
    b->free_ = true;
    b->ownerId_ = NO_OWNER;

    // Merge with the following block if it is free.
    if (b->next_ != 0 && b->next_->free_) {
        MemoryBlock* n = b->next_;
        b->size_ += n->size_;
        b->next_ = n->next_;
        if (n->next_ != 0) n->next_->prev_ = b;
        delete n;
        ++result.merges;
    }
    // Merge into the preceding block if it is free.
    if (b->prev_ != 0 && b->prev_->free_) {
        MemoryBlock* p = b->prev_;
        p->size_ += b->size_;
        p->next_ = b->next_;
        if (b->next_ != 0) b->next_->prev_ = p;
        delete b;
        ++result.merges;
    }
    return result;
}

HeapStats HeapSimulator::stats() const {
    HeapStats s;
    s.total = total_;
    for (const MemoryBlock* b = head_; b != 0; b = b->next_) {
        if (b->free_) {
            ++s.freeBlocks;
            s.freeMemory += b->size_;
            if (b->size_ > s.largestFree) s.largestFree = b->size_;
        } else {
            ++s.usedBlocks;
            s.used += b->size_;
        }
    }
    s.externalFragmentation =
        (s.freeMemory > 0) ? 1.0 - static_cast<double>(s.largestFree) / static_cast<double>(s.freeMemory) : 0.0;
    return s;
}

const MemoryBlock* HeapSimulator::blockAt(MemSize pos) const {
    if (pos < 0 || pos >= total_) return 0;
    for (const MemoryBlock* b = head_; b != 0; b = b->next_) {
        if (pos >= b->start_ && pos < b->start_ + b->size_) return b;
    }
    return 0;
}

bool HeapSimulator::validate(std::string* error) const {
    std::string msg;
    MemSize expected = 0;
    const MemoryBlock* prev = 0;
    for (const MemoryBlock* b = head_; b != 0 && msg.empty(); b = b->next_) {
        if (b->prev_ != prev) {
            msg = util::format("broken prev link at address %lld", b->start_);
        } else if (b->start_ != expected) {
            msg = util::format("gap or overlap: block starts at %lld, expected %lld", b->start_, expected);
        } else if (b->size_ < 1) {
            msg = util::format("empty block at address %lld", b->start_);
        } else if (b->free_ && b->ownerId_ != NO_OWNER) {
            msg = util::format("free block at %lld has an owner", b->start_);
        } else if (!b->free_ && b->ownerId_ < 0) {
            msg = util::format("used block at %lld has no owner", b->start_);
        } else if (b->free_ && prev != 0 && prev->free_) {
            msg = util::format("adjacent free blocks at %lld were not merged", b->start_);
        }
        expected = b->start_ + b->size_;
        prev = b;
    }
    if (msg.empty() && expected != total_) {
        msg = util::format("blocks cover %lld MB but the heap is %lld MB", expected, total_);
    }
    if (msg.empty() && head_ == 0) msg = "heap list is empty";
    if (!msg.empty() && error != 0) *error = msg;
    return msg.empty();
}

}  // namespace heaptrace
