// FragmentationReporter.h - heap snapshots and fragmentation statistics.
//
// All functions return plain text so the same output can go to the console and
// to the session log. The text layout is mirrored exactly by the C version, which
// lets the test scripts diff the two implementations.
#ifndef HEAPTRACE_FRAGMENTATIONREPORTER_H
#define HEAPTRACE_FRAGMENTATIONREPORTER_H

#include "RunMetrics.h"
#include "VirtualMachine.h"

#include <string>
#include <vector>

namespace heaptrace {

class FragmentationReporter {
public:
    // Block-by-block listing followed by the ASCII memory map.
    static std::vector<std::string> snapshotLines(const HeapSimulator& heap, const VMManager& vms);

    // "|AAAA....BB|" - one character per slice of the heap: first letter of the
    // owning VM's name, or '.' for free memory.
    static std::string memoryMap(const HeapSimulator& heap, const VMManager& vms, int width);

    // used / free / free blocks / largest free / external fragmentation.
    static std::string statsLine(const HeapStats& stats);

    // Side-by-side table for the same trace replayed under several strategies.
    static std::vector<std::string> comparisonLines(const std::string& title,
                                                    const std::vector<StrategyRun>& runs);
};

}  // namespace heaptrace

#endif
