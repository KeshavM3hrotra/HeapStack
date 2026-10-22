#include "StrategyComparator.h"

#include "CommandProcessor.h"
#include "FragmentationReporter.h"

#include <sstream>

namespace heaptrace {

std::vector<StrategyRun> StrategyComparator::run(const std::vector<std::string>& lines, MemSize defaultHeap,
                                                 SessionLog* log) {
    static const char* const kStrategies[3] = {"first-fit", "best-fit", "worst-fit"};

    std::vector<StrategyRun> runs;
    for (int s = 0; s < 3; ++s) {
        std::ostringstream sink;  // replays are silent; only the metrics matter
        ProcessorOptions opt;
        opt.printOps = false;
        opt.printStats = false;
        opt.lockStrategy = true;
        CommandProcessor proc(defaultHeap, kStrategies[s], sink, log, opt);
        if (log != 0) log->write(std::string("--- comparison replay: ") + kStrategies[s] + " ---");

        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (proc.execute(lines[i], static_cast<int>(i) + 1) == CommandStatus::Quit) break;
        }

        StrategyRun r;
        r.strategy = kStrategies[s];
        r.heapSize = proc.heap().totalSize();
        r.metrics = proc.metrics();
        r.finalStats = proc.heap().stats();
        r.finalMap = FragmentationReporter::memoryMap(proc.heap(), proc.vms(), MAP_WIDTH);
        runs.push_back(r);
    }
    return runs;
}

}  // namespace heaptrace
