#include "FragmentationReporter.h"

#include "Util.h"

namespace heaptrace {

std::string FragmentationReporter::memoryMap(const HeapSimulator& heap, const VMManager& vms, int width) {
    std::string map = "|";
    const MemSize total = heap.totalSize();
    for (int i = 0; i < width; ++i) {
        // Sample the middle of each slice.
        const MemSize pos = ((2 * static_cast<MemSize>(i) + 1) * total) / (2 * static_cast<MemSize>(width));
        const MemoryBlock* b = heap.blockAt(pos);
        char c = '?';
        if (b != 0) {
            if (b->isFree()) {
                c = '.';
            } else {
                const VirtualMachine* vm = vms.findById(b->ownerId());
                c = (vm != 0 && !vm->name().empty()) ? vm->name()[0] : '?';
            }
        }
        map += c;
    }
    map += '|';
    return map;
}

std::vector<std::string> FragmentationReporter::snapshotLines(const HeapSimulator& heap, const VMManager& vms) {
    std::vector<std::string> lines;
    lines.push_back(util::format("Heap snapshot [%s], total %lld MB", heap.strategy().name().c_str(), heap.totalSize()));
    for (const MemoryBlock* b = heap.head(); b != 0; b = b->next()) {
        std::string line = util::format("  [%6lld,%6lld) %7lld MB  %s", b->start(), b->end(), b->size(),
                                        b->isFree() ? "FREE" : "USED");
        if (!b->isFree()) {
            const VirtualMachine* vm = vms.findById(b->ownerId());
            line += util::format("  %s (vm#%d)", vm != 0 ? vm->name().c_str() : "?", b->ownerId());
        }
        lines.push_back(line);
    }
    lines.push_back("  map " + memoryMap(heap, vms, MAP_WIDTH));
    return lines;
}

std::string FragmentationReporter::statsLine(const HeapStats& s) {
    return util::format("used=%lld MB, free=%lld MB, free blocks=%d, largest free=%lld MB, external fragmentation=%.1f%%",
                        s.used, s.freeMemory, s.freeBlocks, s.largestFree, s.externalFragmentation * 100.0);
}

std::vector<std::string> FragmentationReporter::comparisonLines(const std::string& title,
                                                                const std::vector<StrategyRun>& runs) {
    std::vector<std::string> out;
    out.push_back("=== Strategy comparison: " + title + " ===");
    if (runs.empty()) return out;
    out.push_back(util::format("Heap size: %lld MB", runs[0].heapSize));

    if (runs[0].metrics.errors > 0) {
        out.push_back(util::format("Trace lines skipped because of errors: %d", runs[0].metrics.errors));
        for (std::size_t i = 0; i < runs[0].metrics.errorMessages.size(); ++i) {
            out.push_back("  " + runs[0].metrics.errorMessages[i]);
        }
    }
    out.push_back("");

    out.push_back(util::format("%-10s %6s %9s %10s %9s %12s %10s %10s %8s %9s", "Strategy", "Placed", "Rejected",
                               "(by frag)", "FreeBlks", "LargestFree", "TotalFree", "FinalFrag", "AvgFrag",
                               "PeakFrag"));
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const StrategyRun& r = runs[i];
        out.push_back(util::format("%-10s %6d %9d %10d %9d %12lld %10lld %9.1f%% %7.1f%% %8.1f%%",
                                   r.strategy.c_str(), r.metrics.placed, r.metrics.rejected,
                                   r.metrics.rejectedFragmentation, r.finalStats.freeBlocks,
                                   r.finalStats.largestFree, r.finalStats.freeMemory,
                                   r.finalStats.externalFragmentation * 100.0,
                                   r.metrics.averageFragmentation() * 100.0, r.metrics.fragmentationPeak * 100.0));
    }
    out.push_back("");

    out.push_back("Rejected VMs:");
    for (std::size_t i = 0; i < runs.size(); ++i) {
        std::string list;
        for (std::size_t k = 0; k < runs[i].metrics.rejectedVMs.size(); ++k) {
            if (k > 0) list += ", ";
            list += runs[i].metrics.rejectedVMs[k];
        }
        if (list.empty()) list = "none";
        out.push_back(util::format("  %-10s %s", runs[i].strategy.c_str(), list.c_str()));
    }
    out.push_back("");

    out.push_back("Final memory maps ('.' = free, letter = first letter of the VM name):");
    for (std::size_t i = 0; i < runs.size(); ++i) {
        out.push_back(util::format("  %-10s %s", runs[i].strategy.c_str(), runs[i].finalMap.c_str()));
    }
    out.push_back("");

    int fewest = runs[0].metrics.rejected;
    for (std::size_t i = 1; i < runs.size(); ++i) {
        if (runs[i].metrics.rejected < fewest) fewest = runs[i].metrics.rejected;
    }
    std::string winners;
    int winnerCount = 0;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (runs[i].metrics.rejected == fewest) {
            if (winnerCount > 0) winners += ", ";
            winners += runs[i].strategy;
            ++winnerCount;
        }
    }
    if (winnerCount == static_cast<int>(runs.size())) {
        out.push_back(util::format("All strategies rejected the same number of VMs (%d).", fewest));
    } else {
        out.push_back("Fewest rejected VMs: " + winners + "  (result for this trace only, not a general ranking)");
    }
    return out;
}

}  // namespace heaptrace
