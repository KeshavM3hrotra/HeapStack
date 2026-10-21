#include "CommandProcessor.h"

#include "FragmentationReporter.h"
#include "StrategyComparator.h"
#include "Util.h"

#include <ostream>
#include <stdexcept>

namespace heaptrace {

CommandProcessor::CommandProcessor(MemSize heapSize, const std::string& strategyName, std::ostream& out,
                                   SessionLog* log, const ProcessorOptions& options)
    : out_(out), log_(log), opt_(options), seq_(0) {
    std::unique_ptr<AllocationStrategy> strategy = createStrategy(strategyName);
    if (!strategy) throw std::invalid_argument("unknown strategy '" + strategyName + "'");
    heap_.reset(new HeapSimulator(heapSize));
    heap_->setStrategy(std::move(strategy));
    vms_.reset(new VMManager(*heap_));
    history_.push_back(util::format("HEAP %lld", heapSize));
}

std::vector<std::string> CommandProcessor::helpLines() {
    std::vector<std::string> h;
    h.push_back("Commands (case-insensitive, '#' starts a comment):");
    h.push_back("  HEAP <size_mb>             re-initialise the server memory (discards all VMs)");
    h.push_back("  CREATE <name> <size_mb>    create a VM = allocate a block");
    h.push_back("  TERMINATE <name>           terminate a VM = free its block");
    h.push_back("  STRATEGY <first|best|worst> switch the placement strategy");
    h.push_back("  SNAPSHOT                   show the heap block by block");
    h.push_back("  STATS                      show fragmentation statistics");
    h.push_back("  COMPARE                    replay this session under all strategies");
    h.push_back("  HELP                       show this help");
    h.push_back("  QUIT                       leave");
    return h;
}

void CommandProcessor::say(const std::string& text) { out_ << text << '\n'; }

void CommandProcessor::logLine(const std::string& text) {
    if (log_ != 0) log_->write(text);
}

void CommandProcessor::sample() {
    const HeapStats s = heap_->stats();
    ++metrics_.samples;
    metrics_.fragmentationSum += s.externalFragmentation;
    if (s.externalFragmentation > metrics_.fragmentationPeak) metrics_.fragmentationPeak = s.externalFragmentation;
}

std::vector<std::string> CommandProcessor::snapshotWithHeader() const {
    return FragmentationReporter::snapshotLines(*heap_, *vms_);
}

// Prints and logs the outcome of one command.
void CommandProcessor::report(const std::string& line, const std::string& message, const std::string& status,
                              bool withStats) {
    const std::string stats = FragmentationReporter::statsLine(heap_->stats());
    if (opt_.printOps) {
        say(util::format("%-11s", status.c_str()) + message);
        if (withStats && opt_.printStats) say("           -> " + stats);
    }
    std::string entry = util::format("#%ld %s => %s %s", seq_, line.c_str(), status.c_str(), message.c_str());
    if (withStats) entry += " | " + stats;
    logLine(entry);
}

CommandStatus CommandProcessor::fail(int lineNo, const std::string& line, const std::string& message) {
    ++metrics_.errors;
    const std::string text = util::format("line %d: %s", lineNo, message.c_str());
    metrics_.errorMessages.push_back(text);
    if (opt_.printOps) say(util::format("%-11s", "[ERROR]") + text);
    logLine(util::format("#%ld %s => [ERROR] %s", seq_, line.c_str(), text.c_str()));
    return CommandStatus::Error;
}

CommandStatus CommandProcessor::execute(const std::string& rawLine, int lineNo) {
    std::string text = rawLine;
    const std::size_t hash = text.find('#');
    if (hash != std::string::npos) text.erase(hash);
    const std::string line = util::trim(text);
    if (line.empty()) return CommandStatus::Ignored;

    const std::vector<std::string> tok = util::splitWhitespace(line);
    const std::string cmd = util::toUpper(tok[0]);
    ++seq_;

    if (cmd == "HEAP") return doHeap(tok, line, lineNo);
    if (cmd == "CREATE") return doCreate(tok, line, lineNo);
    if (cmd == "TERMINATE") return doTerminate(tok, line, lineNo);
    if (cmd == "STRATEGY") return doStrategy(tok, line, lineNo);
    if (cmd == "COMPARE") return doCompare(tok, line, lineNo);

    if (cmd == "SNAPSHOT" || cmd == "STATS" || cmd == "HELP" || cmd == "QUIT" || cmd == "EXIT") {
        if (tok.size() != 1) return fail(lineNo, line, "usage: " + cmd + " takes no arguments");
        if (cmd == "QUIT" || cmd == "EXIT") {
            logLine(util::format("#%ld %s => quit", seq_, line.c_str()));
            return CommandStatus::Quit;
        }
        if (cmd == "HELP") {
            if (opt_.printOps) {
                const std::vector<std::string> h = helpLines();
                for (std::size_t i = 0; i < h.size(); ++i) say(h[i]);
            }
            return CommandStatus::Ok;
        }
        if (cmd == "SNAPSHOT") {
            const std::vector<std::string> lines = snapshotWithHeader();
            logLine(util::format("#%ld %s", seq_, line.c_str()));
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (opt_.printOps) say(lines[i]);
                logLine("    " + lines[i]);
            }
            return CommandStatus::Ok;
        }
        // STATS
        const std::string stats = FragmentationReporter::statsLine(heap_->stats());
        if (opt_.printOps) say("Stats: " + stats);
        logLine(util::format("#%ld %s => %s", seq_, line.c_str(), stats.c_str()));
        return CommandStatus::Ok;
    }

    return fail(lineNo, line, "unknown command '" + tok[0] + "' (type HELP for the list)");
}

CommandStatus CommandProcessor::doHeap(const std::vector<std::string>& tok, const std::string& line, int lineNo) {
    MemSize size = 0;
    if (tok.size() != 2) return fail(lineNo, line, "usage: HEAP <size_mb>");
    if (!util::parseSize(tok[1], size)) {
        return fail(lineNo, line, "invalid heap size '" + tok[1] + "' (expected a whole number of MB from 1 to 1000000000000)");
    }
    std::unique_ptr<AllocationStrategy> strategy = createStrategy(heap_->strategy().name());
    std::unique_ptr<HeapSimulator> fresh(new HeapSimulator(size));
    fresh->setStrategy(std::move(strategy));
    vms_.reset();  // the manager refers to the old heap
    heap_.swap(fresh);
    vms_.reset(new VMManager(*heap_));
    history_.clear();
    history_.push_back(util::format("HEAP %lld", size));
    report(line, util::format("HEAP %lld MB -> heap initialised, all VMs discarded", size), "[OK]", false);
    return CommandStatus::Ok;
}

CommandStatus CommandProcessor::doCreate(const std::vector<std::string>& tok, const std::string& line, int lineNo) {
    if (tok.size() != 3) return fail(lineNo, line, "usage: CREATE <name> <size_mb>");
    const std::string& name = tok[1];
    if (!util::isValidVmName(name)) {
        return fail(lineNo, line,
                    "invalid VM name '" + name + "' (1-32 characters: letters, digits, _ - . ; must start with a letter, digit or _)");
    }
    MemSize size = 0;
    if (!util::parseSize(tok[2], size)) {
        return fail(lineNo, line, "invalid size '" + tok[2] + "' (expected a whole number of MB from 1 to 1000000000000)");
    }

    const CreateResult r = vms_->createVM(name, size);
    if (r.duplicate) return fail(lineNo, line, "a VM named '" + name + "' is already running");

    history_.push_back(util::format("CREATE %s %lld", name.c_str(), size));
    if (r.alloc.success) {
        ++metrics_.placed;
        sample();
        report(line,
               util::format("CREATE %s %lld MB -> placed at [%lld, %lld)", name.c_str(), size, r.alloc.start,
                            r.alloc.start + size),
               "[OK]", true);
        return CommandStatus::Ok;
    }

    ++metrics_.rejected;
    metrics_.rejectedVMs.push_back(util::format("%s(%lld MB)", name.c_str(), size));
    const HeapStats s = heap_->stats();
    std::string why;
    if (r.alloc.reason == RejectReason::Fragmentation) {
        ++metrics_.rejectedFragmentation;
        why = util::format("no single free block is large enough (largest free %lld MB, total free %lld MB: external fragmentation)",
                           s.largestFree, s.freeMemory);
    } else {
        ++metrics_.rejectedCapacity;
        why = util::format("only %lld MB free in total (insufficient memory)", s.freeMemory);
    }
    sample();
    report(line, util::format("CREATE %s %lld MB -> %s", name.c_str(), size, why.c_str()), "[REJECTED]", true);
    return CommandStatus::Rejected;
}

CommandStatus CommandProcessor::doTerminate(const std::vector<std::string>& tok, const std::string& line, int lineNo) {
    if (tok.size() != 2) return fail(lineNo, line, "usage: TERMINATE <name>");
    const std::string& name = tok[1];
    if (!util::isValidVmName(name)) return fail(lineNo, line, "invalid VM name '" + name + "'");

    const TerminateResult r = vms_->terminateVM(name);
    if (r.status == TerminateStatus::NotFound) return fail(lineNo, line, "no running VM named '" + name + "'");

    history_.push_back(util::format("TERMINATE %s", name.c_str()));
    if (r.status == TerminateStatus::WasRejected) {
        report(line, util::format("TERMINATE %s -> VM was rejected earlier, nothing to release", name.c_str()),
               "[SKIPPED]", false);
        return CommandStatus::Skipped;
    }

    ++metrics_.terminated;
    sample();
    std::string msg = util::format("TERMINATE %s -> released %lld MB at [%lld, %lld)", name.c_str(), r.release.size,
                                   r.release.start, r.release.start + r.release.size);
    if (r.release.merges > 0) {
        msg += util::format(", merged with %d adjacent free block%s", r.release.merges, r.release.merges == 1 ? "" : "s");
    }
    report(line, msg, "[OK]", true);
    return CommandStatus::Ok;
}

CommandStatus CommandProcessor::doStrategy(const std::vector<std::string>& tok, const std::string& line, int lineNo) {
    if (opt_.lockStrategy) return CommandStatus::Ignored;
    if (tok.size() != 2) return fail(lineNo, line, "usage: STRATEGY <first|best|worst>");
    std::unique_ptr<AllocationStrategy> s = createStrategy(tok[1]);
    if (!s) return fail(lineNo, line, "unknown strategy '" + tok[1] + "' (use first, best or worst)");
    const std::string name = s->name();
    heap_->setStrategy(std::move(s));
    report(line, "STRATEGY -> " + name, "[OK]", false);
    return CommandStatus::Ok;
}

CommandStatus CommandProcessor::doCompare(const std::vector<std::string>& tok, const std::string& line, int lineNo) {
    if (opt_.lockStrategy) return CommandStatus::Ignored;
    if (tok.size() != 1) return fail(lineNo, line, "usage: COMPARE takes no arguments");
    const std::vector<StrategyRun> runs = StrategyComparator::run(history_, heap_->totalSize(), log_);
    const std::vector<std::string> lines = FragmentationReporter::comparisonLines(
        util::format("current session (%d commands)", static_cast<int>(history_.size()) - 1), runs);
    logLine(util::format("#%ld %s", seq_, line.c_str()));
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (opt_.printOps) say(lines[i]);
        logLine("    " + lines[i]);
    }
    return CommandStatus::Ok;
}

void CommandProcessor::finish() {
    const std::vector<std::string> snap = snapshotWithHeader();
    const std::string stats = FragmentationReporter::statsLine(heap_->stats());
    const std::string summary = util::format(
        "Summary: placed=%d, rejected=%d (fragmentation=%d, capacity=%d), terminated=%d, errors=%d", metrics_.placed,
        metrics_.rejected, metrics_.rejectedFragmentation, metrics_.rejectedCapacity, metrics_.terminated,
        metrics_.errors);
    const std::string frag = util::format("External fragmentation over %d samples: average %.1f%%, peak %.1f%%",
                                          metrics_.samples, metrics_.averageFragmentation() * 100.0,
                                          metrics_.fragmentationPeak * 100.0);

    say("");
    say("=== Final heap state ===");
    logLine("=== Final heap state ===");
    for (std::size_t i = 0; i < snap.size(); ++i) {
        say(snap[i]);
        logLine(snap[i]);
    }
    say("Stats: " + stats);
    say(summary);
    say(frag);
    logLine("Stats: " + stats);
    logLine(summary);
    logLine(frag);
}

}  // namespace heaptrace
