// CommandProcessor.h - executes trace / console commands against a simulated server.
//
// The same command language is used for trace files and for the interactive
// console:
//
//   HEAP <size_mb>            (re)initialise the server memory
//   CREATE <name> <size_mb>   place a VM
//   TERMINATE <name>          release a VM
//   STRATEGY <first|best|worst>
//   SNAPSHOT | STATS | COMPARE | HELP | QUIT
//
// '#' starts a comment. Commands are case-insensitive; VM names are not.
#ifndef HEAPTRACE_COMMANDPROCESSOR_H
#define HEAPTRACE_COMMANDPROCESSOR_H

#include "HeapSimulator.h"
#include "RunMetrics.h"
#include "SessionLog.h"
#include "VirtualMachine.h"

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace heaptrace {

enum class CommandStatus {
    Ok,        // executed
    Rejected,  // CREATE refused by the allocator (heap unchanged)
    Skipped,   // TERMINATE of a VM whose creation was rejected earlier
    Error,     // syntax / usage error, line ignored
    Ignored,   // blank line, comment, or command not applicable in this mode
    Quit
};

struct ProcessorOptions {
    bool printOps;       // print one line per command
    bool printStats;     // print fragmentation statistics after CREATE / TERMINATE
    bool lockStrategy;   // ignore STRATEGY and COMPARE (used while replaying for comparison)
    ProcessorOptions() : printOps(true), printStats(true), lockStrategy(false) {}
};

class CommandProcessor {
public:
    // Throws std::invalid_argument for a bad heap size or unknown strategy name.
    // `log` may be null. `out` and `log` must outlive the processor.
    CommandProcessor(MemSize heapSize, const std::string& strategyName, std::ostream& out, SessionLog* log,
                     const ProcessorOptions& options);

    CommandStatus execute(const std::string& rawLine, int lineNo);

    // Final snapshot, statistics and summary (console and log).
    void finish();

    const RunMetrics& metrics() const { return metrics_; }
    const HeapSimulator& heap() const { return *heap_; }
    const VMManager& vms() const { return *vms_; }

    static std::vector<std::string> helpLines();

private:
    CommandStatus doHeap(const std::vector<std::string>& tok, const std::string& line, int lineNo);
    CommandStatus doCreate(const std::vector<std::string>& tok, const std::string& line, int lineNo);
    CommandStatus doTerminate(const std::vector<std::string>& tok, const std::string& line, int lineNo);
    CommandStatus doStrategy(const std::vector<std::string>& tok, const std::string& line, int lineNo);
    CommandStatus doCompare(const std::vector<std::string>& tok, const std::string& line, int lineNo);

    CommandStatus fail(int lineNo, const std::string& line, const std::string& message);
    void report(const std::string& line, const std::string& message, const std::string& status, bool withStats);
    void sample();
    void say(const std::string& text);
    void logLine(const std::string& text);
    std::vector<std::string> snapshotWithHeader() const;

    std::ostream& out_;
    SessionLog* log_;
    ProcessorOptions opt_;
    std::unique_ptr<HeapSimulator> heap_;
    std::unique_ptr<VMManager> vms_;
    RunMetrics metrics_;
    long seq_;
    std::vector<std::string> history_;  // HEAP / CREATE / TERMINATE lines that were accepted
};

}  // namespace heaptrace

#endif
