#include "TraceGenerator.h"

#include "Util.h"

namespace heaptrace {

namespace {

class Lcg {
public:
    explicit Lcg(unsigned long long seed) : state_(seed) {}
    // 31 well-mixed bits.
    unsigned long long next() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return state_ >> 33;
    }

private:
    unsigned long long state_;
};

}  // namespace

std::vector<std::string> generateTrace(MemSize heapSize, int ops, unsigned long long seed) {
    std::vector<std::string> lines;
    lines.push_back(util::format("# HeapTrace generated trace: heap=%lld MB, ops=%d, seed=%llu", heapSize, ops, seed));
    lines.push_back(util::format("HEAP %lld", heapSize));

    Lcg rng(seed);
    MemSize maxSize = heapSize / 10;
    if (maxSize < 1) maxSize = 1;

    std::vector<std::string> live;
    int nextName = 1;
    for (int i = 0; i < ops; ++i) {
        const bool create = live.empty() || (rng.next() % 100) < 50;
        if (create) {
            const MemSize size = 1 + static_cast<MemSize>(rng.next() % static_cast<unsigned long long>(maxSize));
            const std::string name = util::format("V%d", nextName++);
            lines.push_back(util::format("CREATE %s %lld", name.c_str(), size));
            live.push_back(name);
        } else {
            const std::size_t idx = static_cast<std::size_t>(rng.next() % live.size());
            lines.push_back("TERMINATE " + live[idx]);
            live[idx] = live.back();
            live.pop_back();
        }
    }
    return lines;
}

}  // namespace heaptrace
