// HeapTrace (C++) - command-line front end.
//
//   heaptrace_cpp                      interactive console
//   heaptrace_cpp repl    [options]    interactive console
//   heaptrace_cpp run     <trace> [options]
//   heaptrace_cpp compare <trace> [options]
//   heaptrace_cpp gen     [options]    print a generated trace
//   heaptrace_cpp help

#include "CommandProcessor.h"
#include "FragmentationReporter.h"
#include "SessionLog.h"
#include "StrategyComparator.h"
#include "TraceGenerator.h"
#include "Util.h"

#include <ctime>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace heaptrace;

namespace {

const char* const kDefaultLog = "heaptrace_session.log";
const MemSize kDefaultHeap = 1000;

struct Options {
    MemSize heap;
    bool heapGiven;
    std::string strategy;
    std::string logPath;
    bool useLog;
    bool brief;
    long long ops;
    long long seed;
    Options() : heap(kDefaultHeap), heapGiven(false), strategy("first-fit"), logPath(kDefaultLog), useLog(true),
                brief(false), ops(40), seed(1) {}
};

void printUsage(std::ostream& os) {
    os << "HeapTrace - first-fit vs. best-fit memory allocator simulator for single-server VM placement (C++)\n"
          "\n"
          "Usage:\n"
          "  heaptrace_cpp                          interactive console (same as 'repl')\n"
          "  heaptrace_cpp repl    [options]        interactive console\n"
          "  heaptrace_cpp run     <trace> [options]  replay a trace file under one strategy\n"
          "  heaptrace_cpp compare <trace> [options]  replay a trace under first-, best- and worst-fit\n"
          "  heaptrace_cpp gen     [options]        print a pseudo-random trace to standard output\n"
          "  heaptrace_cpp help                     show this text\n"
          "\n"
          "Options:\n"
          "  --heap <mb>        server memory in MB (default 1000; a HEAP line in a trace overrides it)\n"
          "  --strategy <name>  first | best | worst   (run / repl, default first)\n"
          "  --log <file>       session log file (default heaptrace_session.log)\n"
          "  --no-log           do not write a session log\n"
          "  --brief            run: omit the statistics line after each operation\n"
          "  --ops <n>          gen: number of operations, 1..1000000 (default 40)\n"
          "  --seed <n>         gen: random seed (default 1)\n"
          "\n"
          "Trace commands: HEAP, CREATE, TERMINATE, STRATEGY, SNAPSHOT, STATS, COMPARE, HELP, QUIT\n";
}

bool parseOptions(const std::vector<std::string>& args, std::size_t first, Options& o, std::string& error) {
    for (std::size_t i = first; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool needsValue = (a == "--heap" || a == "--strategy" || a == "--log" || a == "--ops" || a == "--seed");
        if (needsValue && i + 1 >= args.size()) {
            error = "option " + a + " needs a value";
            return false;
        }
        if (a == "--heap") {
            if (!util::parseSize(args[++i], o.heap)) {
                error = "invalid --heap value '" + args[i] + "' (whole number of MB from 1 to 1000000000000)";
                return false;
            }
            o.heapGiven = true;
        } else if (a == "--strategy") {
            o.strategy = args[++i];
            if (!createStrategy(o.strategy)) {
                error = "unknown strategy '" + o.strategy + "' (use first, best or worst)";
                return false;
            }
        } else if (a == "--log") {
            o.logPath = args[++i];
            o.useLog = true;
        } else if (a == "--no-log") {
            o.useLog = false;
        } else if (a == "--brief") {
            o.brief = true;
        } else if (a == "--ops") {
            if (!util::parseInteger(args[++i], 1, 1000000, o.ops)) {
                error = "invalid --ops value '" + args[i] + "' (1 to 1000000)";
                return false;
            }
        } else if (a == "--seed") {
            if (!util::parseInteger(args[++i], 0, 999999999999999999LL, o.seed)) {
                error = "invalid --seed value '" + args[i] + "' (a non-negative whole number)";
                return false;
            }
        } else {
            error = "unknown option '" + a + "'";
            return false;
        }
    }
    return true;
}

// Reads all lines of a text file (handles CRLF and a UTF-8 byte-order mark).
bool readLines(const std::string& path, std::vector<std::string>& lines) {
    std::ifstream in(path.c_str());
    if (!in.is_open()) return false;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        first = false;
        lines.push_back(line);
    }
    return true;
}

void openLog(SessionLog& log, const Options& o, const std::string& mode) {
    if (!o.useLog) return;
    if (!log.open(o.logPath)) {
        std::cerr << "warning: cannot open log file '" << o.logPath << "'; continuing without a session log\n";
        return;
    }
    char stamp[64] = "unknown time";
    const std::time_t now = std::time(0);
    const std::tm* local = std::localtime(&now);
    if (local != 0) std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", local);
    log.write("HeapTrace session log (C++ implementation)");
    log.write(std::string("started: ") + stamp);
    log.write("mode: " + mode);
}

void announceLog(const SessionLog& log) {
    if (log.isOpen()) std::cout << "Session log written to " << log.path() << "\n";
}

int cmdRun(const std::string& path, const Options& o) {
    std::vector<std::string> lines;
    if (!readLines(path, lines)) {
        std::cerr << "error: cannot open trace file '" << path << "'\n";
        return 1;
    }
    SessionLog log;
    openLog(log, o, "run " + path);
    ProcessorOptions po;
    po.printStats = !o.brief;
    CommandProcessor proc(o.heap, o.strategy, std::cout, o.useLog ? &log : 0, po);
    std::cout << "HeapTrace run: " << path << " | strategy " << proc.heap().strategy().name() << " | default heap "
              << o.heap << " MB\n";
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (proc.execute(lines[i], static_cast<int>(i) + 1) == CommandStatus::Quit) break;
    }
    proc.finish();
    announceLog(log);
    return 0;
}

int cmdCompare(const std::string& path, const Options& o) {
    std::vector<std::string> lines;
    if (!readLines(path, lines)) {
        std::cerr << "error: cannot open trace file '" << path << "'\n";
        return 1;
    }
    SessionLog log;
    openLog(log, o, "compare " + path);
    const std::vector<StrategyRun> runs = StrategyComparator::run(lines, o.heap, o.useLog ? &log : 0);
    const std::vector<std::string> out = FragmentationReporter::comparisonLines(path, runs);
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::cout << out[i] << "\n";
        log.write(out[i]);
    }
    announceLog(log);
    return 0;
}

int cmdRepl(const Options& o) {
    SessionLog log;
    openLog(log, o, "interactive console");
    ProcessorOptions po;
    CommandProcessor proc(o.heap, o.strategy, std::cout, o.useLog ? &log : 0, po);
    std::cout << "HeapTrace - single-server VM placement simulator (C++)\n"
              << "Server memory: " << o.heap << " MB, strategy: " << proc.heap().strategy().name()
              << ". Type HELP for commands, QUIT to exit.\n";
    std::string line;
    int lineNo = 0;
    while (true) {
        std::cout << "heaptrace> " << std::flush;
        if (!std::getline(std::cin, line)) break;  // end of input
        ++lineNo;
        if (proc.execute(line, lineNo) == CommandStatus::Quit) break;
    }
    proc.finish();
    announceLog(log);
    return 0;
}

int cmdGen(const Options& o) {
    const std::vector<std::string> lines =
        generateTrace(o.heap, static_cast<int>(o.ops), static_cast<unsigned long long>(o.seed));
    for (std::size_t i = 0; i < lines.size(); ++i) std::cout << lines[i] << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

    try {
        if (args.empty()) return cmdRepl(Options());

        const std::string cmd = util::toLower(args[0]);
        if (cmd == "help" || cmd == "--help" || cmd == "-h" || cmd == "/?") {
            printUsage(std::cout);
            return 0;
        }

        Options opt;
        std::string error;
        if (cmd == "run" || cmd == "compare") {
            if (args.size() < 2 || args[1].compare(0, 2, "--") == 0) {
                std::cerr << "error: '" << cmd << "' needs a trace file\n\n";
                printUsage(std::cerr);
                return 2;
            }
            if (!parseOptions(args, 2, opt, error)) {
                std::cerr << "error: " << error << "\n\n";
                printUsage(std::cerr);
                return 2;
            }
            return cmd == "run" ? cmdRun(args[1], opt) : cmdCompare(args[1], opt);
        }
        if (cmd == "repl" || cmd == "gen") {
            if (!parseOptions(args, 1, opt, error)) {
                std::cerr << "error: " << error << "\n\n";
                printUsage(std::cerr);
                return 2;
            }
            return cmd == "repl" ? cmdRepl(opt) : cmdGen(opt);
        }

        std::cerr << "error: unknown command '" << args[0] << "'\n\n";
        printUsage(std::cerr);
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "fatal error: " << e.what() << "\n";
        return 1;
    }
}
