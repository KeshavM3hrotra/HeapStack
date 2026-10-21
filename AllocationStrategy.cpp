#include "AllocationStrategy.h"

#include "Util.h"

namespace heaptrace {

MemoryBlock* FirstFitStrategy::selectBlock(MemoryBlock* head, MemSize size) const {
    for (MemoryBlock* b = head; b != 0; b = b->next()) {
        if (b->isFree() && b->size() >= size) return b;
    }
    return 0;
}

MemoryBlock* BestFitStrategy::selectBlock(MemoryBlock* head, MemSize size) const {
    MemoryBlock* best = 0;
    for (MemoryBlock* b = head; b != 0; b = b->next()) {
        if (b->isFree() && b->size() >= size && (best == 0 || b->size() < best->size())) best = b;
    }
    return best;
}

MemoryBlock* WorstFitStrategy::selectBlock(MemoryBlock* head, MemSize size) const {
    MemoryBlock* worst = 0;
    for (MemoryBlock* b = head; b != 0; b = b->next()) {
        if (b->isFree() && b->size() >= size && (worst == 0 || b->size() > worst->size())) worst = b;
    }
    return worst;
}

std::unique_ptr<AllocationStrategy> createStrategy(const std::string& name) {
    const std::string n = util::toLower(name);
    if (n == "first" || n == "first-fit" || n == "firstfit") {
        return std::unique_ptr<AllocationStrategy>(new FirstFitStrategy());
    }
    if (n == "best" || n == "best-fit" || n == "bestfit") {
        return std::unique_ptr<AllocationStrategy>(new BestFitStrategy());
    }
    if (n == "worst" || n == "worst-fit" || n == "worstfit") {
        return std::unique_ptr<AllocationStrategy>(new WorstFitStrategy());
    }
    return std::unique_ptr<AllocationStrategy>();
}

}  // namespace heaptrace
