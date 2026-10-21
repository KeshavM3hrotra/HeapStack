// RunMetrics.h - counters collected while a trace is replayed.
#ifndef HEAPTRACE_RUNMETRICS_H
#define HEAPTRACE_RUNMETRICS_H

#include "HeapSimulator.h"

#include <string>
#include <vector>

namespace heaptrace {

struct RunMetrics {
    int placed;                 // VMs successfully created
    int rejected;               // VMs refused (fragmentation + capacity)
    int rejectedFragmentation;  // refused although enough memory was free in total
    int rejectedCapacity;       // refused because too little memory was free
    int terminated;             // VMs terminated (blocks released)
    int errors;                 // trace lines skipped because of an error
    int samples;                // number of fragmentation samples taken
    double fragmentationSum;    // sum of external fragmentation over all samples
    double fragmentationPeak;   // highest external fragmentation seen
    std::vector<std::string> rejectedVMs;     // e.g. "F(290 MB)"
    std::vector<std::string> errorMessages;   // e.g. "line 7: unknown command 'FOO'"

    RunMetrics()
        : placed(0), rejected(0), rejectedFragmentation(0), rejectedCapacity(0), terminated(0),
          errors(0), samples(0), fragmentationSum(0.0), fragmentationPeak(0.0) {}

    double averageFragmentation() const { return samples > 0 ? fragmentationSum / samples : 0.0; }
};

// Result of replaying one trace under one strategy.
struct StrategyRun {
    std::string strategy;
    MemSize heapSize;
    RunMetrics metrics;
    HeapStats finalStats;
    std::string finalMap;  // ASCII memory map of the final heap

    StrategyRun() : heapSize(0) {}
};

}  // namespace heaptrace

#endif
