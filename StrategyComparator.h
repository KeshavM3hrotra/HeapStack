// StrategyComparator.h - replays one trace under first-fit, best-fit and worst-fit.
#ifndef HEAPTRACE_STRATEGYCOMPARATOR_H
#define HEAPTRACE_STRATEGYCOMPARATOR_H

#include "RunMetrics.h"
#include "SessionLog.h"

#include <string>
#include <vector>

namespace heaptrace {

class StrategyComparator {
public:
    // Replays `lines` (trace lines, in order) on a fresh heap once per strategy.
    // `defaultHeap` is used until the trace sets its own size with HEAP.
    // STRATEGY / COMPARE lines inside the trace are ignored. `log` may be null.
    // Results are in the order first-fit, best-fit, worst-fit.
    static std::vector<StrategyRun> run(const std::vector<std::string>& lines, MemSize defaultHeap, SessionLog* log);
};

}  // namespace heaptrace

#endif
