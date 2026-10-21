// AllocationStrategy.h - the pluggable placement policy (run-time polymorphism).
//
// HeapSimulator calls selectBlock() through a base-class pointer, so the policy
// can be swapped at run time without touching the engine.
//
// Tie-breaking is deterministic: when several free blocks qualify equally, the
// one with the lowest address wins.
#ifndef HEAPTRACE_ALLOCATIONSTRATEGY_H
#define HEAPTRACE_ALLOCATIONSTRATEGY_H

#include "MemoryBlock.h"

#include <memory>
#include <string>

namespace heaptrace {

class AllocationStrategy {
public:
    virtual ~AllocationStrategy() {}

    // Returns the free block that should serve a request of `size` MB, or a
    // null pointer when no free block is large enough. Must not modify the list.
    virtual MemoryBlock* selectBlock(MemoryBlock* head, MemSize size) const = 0;

    virtual std::string name() const = 0;
};

// First free block (lowest address) that is large enough.
class FirstFitStrategy : public AllocationStrategy {
public:
    MemoryBlock* selectBlock(MemoryBlock* head, MemSize size) const override;
    std::string name() const override { return "first-fit"; }
};

// Smallest free block that is large enough.
class BestFitStrategy : public AllocationStrategy {
public:
    MemoryBlock* selectBlock(MemoryBlock* head, MemSize size) const override;
    std::string name() const override { return "best-fit"; }
};

// Largest free block (stretch goal in the project plan).
class WorstFitStrategy : public AllocationStrategy {
public:
    MemoryBlock* selectBlock(MemoryBlock* head, MemSize size) const override;
    std::string name() const override { return "worst-fit"; }
};

// Accepts first | first-fit | best | best-fit | worst | worst-fit (any case).
// Returns a null pointer for an unknown name.
std::unique_ptr<AllocationStrategy> createStrategy(const std::string& name);

}  // namespace heaptrace

#endif
