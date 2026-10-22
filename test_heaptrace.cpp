// Self-contained unit/integration tests for the C++ HeapTrace implementation.
// No test framework needed: build and run, exit code 0 means every check passed.

#include "CommandProcessor.h"
#include "FragmentationReporter.h"
#include "HeapSimulator.h"
#include "SessionLog.h"
#include "StrategyComparator.h"
#include "TraceGenerator.h"
#include "Util.h"
#include "VirtualMachine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace heaptrace;

static int g_checks = 0;
static int g_failures = 0;

// ---- test-only leak detector: count every operator new / delete in the program ----
static long g_outstanding = 0;

void* operator new(std::size_t n) {
    void* p = std::malloc(n != 0 ? n : 1);
    if (p == 0) throw std::bad_alloc();
    ++g_outstanding;
    return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept {
    if (p != 0) {
        --g_outstanding;
        std::free(p);
    }
}
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(cond)) {                                                                    \
            ++g_failures;                                                                 \
            std::cerr << "  FAILED " << __FILE__ << ":" << __LINE__ << "  " #cond << "\n"; \
        }                                                                                 \
    } while (0)

#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) < 1e-9)

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static void testUtil() {
    std::cout << "util\n";
    MemSize v = 0;
    CHECK(util::parseSize("1", v) && v == 1);
    CHECK(util::parseSize("1000", v) && v == 1000);
    CHECK(util::parseSize("1000000000000", v) && v == 1000000000000LL);
    CHECK(!util::parseSize("1000000000001", v));
    CHECK(!util::parseSize("0", v));
    CHECK(!util::parseSize("-1", v));
    CHECK(!util::parseSize("", v));
    CHECK(!util::parseSize("12a", v));
    CHECK(!util::parseSize("1.5", v));
    CHECK(!util::parseSize(" 5", v));
    CHECK(!util::parseSize("99999999999999999999", v));  // would overflow without the length guard
    CHECK(util::isValidVmName("A"));
    CHECK(util::isValidVmName("web-1.prod_x"));
    CHECK(util::isValidVmName("_x"));
    CHECK(!util::isValidVmName(""));
    CHECK(!util::isValidVmName("-x"));
    CHECK(!util::isValidVmName(".x"));
    CHECK(!util::isValidVmName("a b"));
    CHECK(!util::isValidVmName("a#b"));
    CHECK(!util::isValidVmName(std::string(33, 'a')));
    CHECK(util::isValidVmName(std::string(32, 'a')));
    CHECK(util::trim("  \t x y \r\n") == "x y");
    CHECK(util::splitWhitespace("  a\tb   c ").size() == 3);
    CHECK(util::splitWhitespace("   ").empty());
    CHECK(util::toUpper("cReate") == "CREATE");
    CHECK(util::toLower("BeSt-Fit") == "best-fit");
}

static void testHeapBasics() {
    std::cout << "heap basics: split, exact fit, rejection reasons, invalid requests\n";
    const long base = MemoryBlock::liveCount();
    {
        HeapSimulator h(100);
        CHECK(h.validate());
        HeapStats s = h.stats();
        CHECK(s.total == 100 && s.freeBlocks == 1 && s.largestFree == 100 && s.usedBlocks == 0);
        CHECK_NEAR(s.externalFragmentation, 0.0);

        AllocationResult r = h.allocate(0, 30);
        CHECK(r.success && r.start == 0 && r.reason == RejectReason::None);
        s = h.stats();
        CHECK(s.used == 30 && s.freeMemory == 70 && s.freeBlocks == 1 && s.usedBlocks == 1);
        CHECK(h.validate());

        r = h.allocate(1, 70);  // exact fit: no split, no leftover block
        CHECK(r.success && r.start == 30);
        s = h.stats();
        CHECK(s.freeBlocks == 0 && s.freeMemory == 0 && s.largestFree == 0);
        CHECK_NEAR(s.externalFragmentation, 0.0);  // nothing free: defined as 0, not NaN
        CHECK(h.validate());

        r = h.allocate(2, 1);
        CHECK(!r.success && r.reason == RejectReason::InsufficientMemory && r.start == -1);

        CHECK(h.allocate(-1, 5).reason == RejectReason::InvalidRequest);
        CHECK(h.allocate(5, 0).reason == RejectReason::InvalidRequest);
        CHECK(h.allocate(5, -3).reason == RejectReason::InvalidRequest);
        CHECK(h.allocate(0, 1).reason == RejectReason::InvalidRequest);  // owner 0 already holds a block
        CHECK(h.validate());

        CHECK(!h.release(99).found);
        CHECK(!h.release(-1).found);
        CHECK(h.blockAt(0) != 0 && h.blockAt(99) != 0);
        CHECK(h.blockAt(-1) == 0 && h.blockAt(100) == 0);
    }
    CHECK(MemoryBlock::liveCount() == base);  // destructor freed every node

    bool threw = false;
    try { HeapSimulator bad(0); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { HeapSimulator bad(-5); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { HeapSimulator bad(MAX_MEMORY_MB + 1); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    CHECK(MemoryBlock::liveCount() == base);  // a failed constructor leaks nothing
}

static void testCoalescing() {
    std::cout << "coalescing: left, right, both, none\n";
    const long base = MemoryBlock::liveCount();
    {
        HeapSimulator h(40);
        for (int i = 0; i < 4; ++i) CHECK(h.allocate(i, 10).success);  // A B C D

        ReleaseResult r = h.release(1);  // B: neighbours A and C are used -> no merge
        CHECK(r.found && r.merges == 0 && r.start == 10 && r.size == 10);
        CHECK(h.stats().freeBlocks == 1 && h.validate());

        r = h.release(3);  // D: last block, previous neighbour C is used -> no merge
        CHECK(r.found && r.merges == 0);
        CHECK(h.stats().freeBlocks == 2 && h.validate());

        r = h.release(2);  // C: free neighbours on both sides -> merges twice
        CHECK(r.found && r.merges == 2);
        CHECK(h.stats().freeBlocks == 1 && h.stats().largestFree == 30 && h.validate());

        r = h.release(0);  // A: merges with the free block on its right
        CHECK(r.found && r.merges == 1);
        CHECK(h.stats().freeBlocks == 1 && h.stats().largestFree == 40 && h.head()->isFree() &&
              h.head()->size() == 40 && h.head()->next() == 0);
        CHECK(h.validate());

        CHECK(!h.release(0).found);  // double release is rejected, not corrupting
    }
    {
        HeapSimulator h(30);
        for (int i = 0; i < 3; ++i) CHECK(h.allocate(i, 10).success);
        CHECK(h.release(0).merges == 0);
        CHECK(h.release(1).merges == 1);  // merges with the free block on its LEFT
        CHECK(h.release(2).merges == 1);
        CHECK(h.stats().freeBlocks == 1 && h.stats().largestFree == 30 && h.validate());
    }
    CHECK(MemoryBlock::liveCount() == base);
}

// Layout used by the strategy tests: free holes of 100 @0, 200 @150, 300 @400, 250 @750.
static void buildHoles(HeapSimulator& h) {
    const MemSize sizes[6] = {100, 50, 200, 50, 300, 50};
    for (int i = 0; i < 6; ++i) CHECK(h.allocate(i, sizes[i]).success);
    CHECK(h.release(0).found);
    CHECK(h.release(2).found);
    CHECK(h.release(4).found);
    CHECK(h.stats().freeBlocks == 4 && h.stats().freeMemory == 850);
}

static MemSize placeWith(const char* strategy, MemSize request) {
    HeapSimulator h(1000);
    buildHoles(h);
    h.setStrategy(createStrategy(strategy));
    const AllocationResult r = h.allocate(100, request);
    CHECK(h.validate());
    return r.success ? r.start : -1;
}

static void testStrategies() {
    std::cout << "strategies: first / best / worst, ties, run-time switching\n";
    CHECK(createStrategy("first") && createStrategy("FIRST-FIT") && createStrategy("firstfit"));
    CHECK(createStrategy("Best") && createStrategy("best-fit"));
    CHECK(createStrategy("worst") && createStrategy("worst-fit"));
    CHECK(!createStrategy("") && !createStrategy("next-fit") && !createStrategy("fit"));
    CHECK(createStrategy("best")->name() == "best-fit");

    // request 90: holes 100@0 200@150 300@400 250@750
    CHECK(placeWith("first", 90) == 0);
    CHECK(placeWith("best", 90) == 0);
    CHECK(placeWith("worst", 90) == 400);
    // request 150
    CHECK(placeWith("first", 150) == 150);
    CHECK(placeWith("best", 150) == 150);
    CHECK(placeWith("worst", 150) == 400);
    // request 220: first-fit takes the 300 hole, best-fit the 250 hole
    CHECK(placeWith("first", 220) == 400);
    CHECK(placeWith("best", 220) == 750);
    CHECK(placeWith("worst", 220) == 400);
    // request that fits nowhere (301 > largest hole 300) although 850 MB are free
    CHECK(placeWith("first", 301) == -1);
    CHECK(placeWith("best", 301) == -1);
    CHECK(placeWith("worst", 301) == -1);

    {  // fragmentation vs capacity reasons
        HeapSimulator h(1000);
        buildHoles(h);
        AllocationResult r = h.allocate(100, 301);
        CHECK(!r.success && r.reason == RejectReason::Fragmentation);
        r = h.allocate(100, 851);
        CHECK(!r.success && r.reason == RejectReason::InsufficientMemory);
        CHECK(h.validate());
    }
    {  // ties: lowest address wins for best-fit and worst-fit
        HeapSimulator h(100);
        CHECK(h.allocate(0, 10).success);   // [0,10)
        CHECK(h.allocate(1, 20).success);   // [10,30)
        CHECK(h.allocate(2, 10).success);   // [30,40)
        CHECK(h.allocate(3, 20).success);   // [40,60)
        CHECK(h.allocate(4, 40).success);   // [60,100)
        CHECK(h.release(1).found);          // hole 20 @10
        CHECK(h.release(3).found);          // hole 20 @40
        h.setStrategy(createStrategy("best"));
        CHECK(h.allocate(5, 20).start == 10);
        CHECK(h.release(5).found);
        h.setStrategy(createStrategy("worst"));
        CHECK(h.allocate(6, 5).start == 10);
        CHECK(h.validate());
    }
    {  // run-time switch keeps the heap intact; null is ignored
        HeapSimulator h(100);
        CHECK(h.strategy().name() == "first-fit");
        h.setStrategy(createStrategy("best"));
        CHECK(h.strategy().name() == "best-fit");
        h.setStrategy(std::unique_ptr<AllocationStrategy>());
        CHECK(h.strategy().name() == "best-fit");
        CHECK(h.allocate(0, 10).success && h.validate());
    }
}

static void testVmManager() {
    std::cout << "VM manager\n";
    HeapSimulator h(100);
    VMManager m(h);

    CreateResult a = m.createVM("A", 40);
    CHECK(!a.duplicate && a.id == 0 && a.alloc.success && a.alloc.start == 0);
    CreateResult dup = m.createVM("A", 10);
    CHECK(dup.duplicate && dup.id == -1);
    CHECK(m.requestCount() == 1);

    CreateResult big = m.createVM("BIG", 200);
    CHECK(!big.duplicate && big.id == 1 && !big.alloc.success && big.alloc.reason == RejectReason::InsufficientMemory);
    CHECK(m.findById(1) != 0 && m.findById(1)->state() == VMState::Rejected);
    CHECK(m.runningCount() == 1);

    TerminateResult t = m.terminateVM("BIG");
    CHECK(t.status == TerminateStatus::WasRejected);
    CHECK(m.terminateVM("nobody").status == TerminateStatus::NotFound);

    t = m.terminateVM("A");
    CHECK(t.status == TerminateStatus::Terminated && t.release.found && t.release.size == 40);
    CHECK(m.terminateVM("A").status == TerminateStatus::NotFound);  // already terminated
    CHECK(m.findById(0)->state() == VMState::Terminated);
    CHECK(m.runningCount() == 0);

    CreateResult again = m.createVM("A", 60);  // a terminated name may be reused, with a new id
    CHECK(!again.duplicate && again.id == 2 && again.alloc.success);
    CHECK(m.findById(2)->state() == VMState::Running && m.findById(2)->start() == again.alloc.start);
    CHECK(m.findById(-1) == 0 && m.findById(99) == 0);
    CHECK(h.validate());
}

static CommandStatus run(CommandProcessor& p, const std::string& line) {
    static int n = 0;
    return p.execute(line, ++n);
}

static void testWorkedExample() {
    std::cout << "worked example from the presentation (1000 MB server)\n";
    const char* const setup[] = {"HEAP 1000", "CREATE A 300", "CREATE B 100", "CREATE C 200", "CREATE D 150",
                                 "TERMINATE A", "TERMINATE C", "CREATE E 180"};
    for (int pass = 0; pass < 2; ++pass) {
        const bool best = (pass == 1);
        std::ostringstream out;
        CommandProcessor p(1000, best ? "best" : "first", out, 0, ProcessorOptions());
        for (std::size_t i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) CHECK(run(p, setup[i]) == CommandStatus::Ok);

        const HeapStats s = p.heap().stats();
        CHECK(s.freeBlocks == 3 && s.freeMemory == 570 && s.used == 430);
        if (!best) {
            CHECK(s.largestFree == 250);
            CHECK_NEAR(s.externalFragmentation, 1.0 - 250.0 / 570.0);
            CHECK(contains(FragmentationReporter::statsLine(s), "external fragmentation=56.1%"));
            CHECK(run(p, "CREATE F 290") == CommandStatus::Rejected);  // 570 MB free, largest hole 250
            CHECK(p.metrics().rejectedFragmentation == 1 && p.metrics().rejectedCapacity == 0);
            CHECK(contains(out.str(), "external fragmentation)"));
            CHECK(p.vms().findById(4)->start() == 0);  // E went into the first (300 MB) hole
        } else {
            CHECK(s.largestFree == 300);
            CHECK_NEAR(s.externalFragmentation, 1.0 - 300.0 / 570.0);
            CHECK(contains(FragmentationReporter::statsLine(s), "external fragmentation=47.4%"));
            CHECK(p.vms().findById(4)->start() == 400);  // E went into the tightest (200 MB) hole
            CHECK(run(p, "CREATE F 290") == CommandStatus::Ok);
            CHECK(p.vms().findById(5)->start() == 0);
        }
        CHECK(p.heap().validate());
    }
}

static void testProcessor() {
    std::cout << "command processor: errors leave the heap unchanged\n";
    std::ostringstream out;
    CommandProcessor p(500, "first", out, 0, ProcessorOptions());
    CHECK(run(p, "CREATE A 100") == CommandStatus::Ok);
    const std::string before = FragmentationReporter::statsLine(p.heap().stats());

    const char* const bad[] = {"CREATE", "CREATE X", "CREATE X 10 20", "CREATE X 0", "CREATE X -1", "CREATE X abc",
                               "CREATE X 1e3", "CREATE -X 5", "CREATE A 5", "TERMINATE", "TERMINATE Z",
                               "TERMINATE A B", "HEAP", "HEAP 0", "HEAP -5", "HEAP 10x", "STRATEGY", "STRATEGY foo",
                               "STRATEGY best extra", "BOGUS", "SNAPSHOT now", "QUIT now"};
    for (std::size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(run(p, bad[i]) == CommandStatus::Error);
    }
    CHECK(p.metrics().errors == static_cast<int>(sizeof(bad) / sizeof(bad[0])));
    CHECK(FragmentationReporter::statsLine(p.heap().stats()) == before);
    CHECK(p.heap().validate());

    CHECK(run(p, "") == CommandStatus::Ignored);
    CHECK(run(p, "   \t ") == CommandStatus::Ignored);
    CHECK(run(p, "# only a comment") == CommandStatus::Ignored);
    CHECK(run(p, "create B 50   # trailing comment") == CommandStatus::Ok);
    CHECK(run(p, "CREATE C 20\r") == CommandStatus::Ok);  // CRLF line endings
    CHECK(run(p, "  terminate   B  ") == CommandStatus::Ok);
    CHECK(run(p, "SNAPSHOT") == CommandStatus::Ok);
    CHECK(run(p, "stats") == CommandStatus::Ok);
    CHECK(run(p, "help") == CommandStatus::Ok);
    CHECK(run(p, "STRATEGY WORST") == CommandStatus::Ok);
    CHECK(p.heap().strategy().name() == "worst-fit");
    CHECK(run(p, "HEAP 80") == CommandStatus::Ok);  // re-initialise keeps the strategy
    CHECK(p.heap().totalSize() == 80 && p.heap().stats().freeBlocks == 1 && p.heap().strategy().name() == "worst-fit");
    CHECK(p.vms().requestCount() == 0);
    CHECK(run(p, "CREATE A 80") == CommandStatus::Ok);  // name A is free again after the reset
    CHECK(run(p, "COMPARE") == CommandStatus::Ok);
    CHECK(contains(out.str(), "=== Strategy comparison: current session"));
    CHECK(run(p, "quit") == CommandStatus::Quit);
    CHECK(run(p, "exit") == CommandStatus::Quit);

    // Locked mode (used for replays) ignores STRATEGY and COMPARE.
    std::ostringstream sink;
    ProcessorOptions locked;
    locked.printOps = false;
    locked.lockStrategy = true;
    CommandProcessor q(100, "best", sink, 0, locked);
    CHECK(run(q, "STRATEGY first") == CommandStatus::Ignored);
    CHECK(run(q, "COMPARE") == CommandStatus::Ignored);
    CHECK(q.heap().strategy().name() == "best-fit");
    CHECK(run(q, "CREATE A 10") == CommandStatus::Ok);
    CHECK(sink.str().empty());  // silent

    bool threw = false;
    try { CommandProcessor bad2(100, "nonsense", sink, 0, ProcessorOptions()); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { CommandProcessor bad3(0, "first", sink, 0, ProcessorOptions()); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

// Deterministic generator for the stress test (independent of TraceGenerator).
static unsigned long long g_state = 12345;
static unsigned long long nextRandom() {
    g_state = g_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return g_state >> 33;
}

static void testStress() {
    std::cout << "randomised stress test with invariant checks after every operation\n";
    const long base = MemoryBlock::liveCount();
    const char* const names[3] = {"first", "best", "worst"};
    for (int s = 0; s < 3; ++s) {
        g_state = 987654321ULL + static_cast<unsigned long long>(s);
        {
            HeapSimulator h(5000);
            h.setStrategy(createStrategy(names[s]));
            std::map<int, MemSize> live;  // owner id -> size (the model)
            int nextId = 0;
            bool ok = true;
            for (int step = 0; step < 30000 && ok; ++step) {
                if (live.empty() || nextRandom() % 100 < 55) {
                    const MemSize size = 1 + static_cast<MemSize>(nextRandom() % 700);
                    const AllocationResult r = h.allocate(nextId, size);
                    if (r.success) {
                        live[nextId] = size;
                        const MemoryBlock* b = h.blockAt(r.start);
                        ok = ok && b != 0 && !b->isFree() && b->ownerId() == nextId && b->start() == r.start &&
                             b->size() == size;
                    } else {
                        const HeapStats st = h.stats();
                        ok = ok && (r.reason == (st.freeMemory >= size ? RejectReason::Fragmentation
                                                                       : RejectReason::InsufficientMemory));
                        ok = ok && st.largestFree < size;  // it really did not fit anywhere
                    }
                    ++nextId;
                } else {
                    std::map<int, MemSize>::iterator it = live.begin();
                    std::advance(it, static_cast<long>(nextRandom() % live.size()));
                    const ReleaseResult r = h.release(it->first);
                    ok = ok && r.found && r.size == it->second;
                    live.erase(it);
                }
                MemSize used = 0;
                for (std::map<int, MemSize>::const_iterator it = live.begin(); it != live.end(); ++it) used += it->second;
                const HeapStats st = h.stats();
                ok = ok && h.validate() && st.used == used && st.used + st.freeMemory == 5000 &&
                     st.externalFragmentation >= 0.0 && st.externalFragmentation <= 1.0;
            }
            CHECK(ok);
            // Drain: releasing everything must coalesce back to one free block.
            for (std::map<int, MemSize>::const_iterator it = live.begin(); it != live.end(); ++it) {
                CHECK(h.release(it->first).found);
            }
            const HeapStats st = h.stats();
            CHECK(st.freeBlocks == 1 && st.largestFree == 5000 && st.usedBlocks == 0 && h.validate());
        }
        CHECK(MemoryBlock::liveCount() == base);
    }
}

static void testComparatorAndGenerator() {
    std::cout << "comparator and trace generator\n";
    std::vector<std::string> trace;
    const char* const lines[] = {"HEAP 1000", "CREATE A 300", "CREATE B 100", "CREATE C 200", "CREATE D 150",
                                 "TERMINATE A", "TERMINATE C", "STRATEGY best", "CREATE E 180", "CREATE F 290", "COMPARE"};
    for (std::size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) trace.push_back(lines[i]);

    const std::vector<StrategyRun> runs = StrategyComparator::run(trace, 1000, 0);
    CHECK(runs.size() == 3);
    CHECK(runs[0].strategy == "first-fit" && runs[1].strategy == "best-fit" && runs[2].strategy == "worst-fit");
    CHECK(runs[0].metrics.rejected == 1 && runs[0].metrics.rejectedVMs.size() == 1 &&
          runs[0].metrics.rejectedVMs[0] == "F(290 MB)");
    CHECK(runs[1].metrics.rejected == 0 && runs[1].metrics.placed == 6);
    CHECK(runs[2].metrics.rejected == 1);
    CHECK(runs[0].metrics.errors == 0 && runs[1].metrics.errors == 0 && runs[2].metrics.errors == 0);  // STRATEGY/COMPARE ignored
    CHECK(runs[0].finalStats.freeBlocks == 3 && runs[0].finalStats.largestFree == 250);
    CHECK(runs[1].heapSize == 1000);
    CHECK(runs[0].finalMap.size() == static_cast<std::size_t>(MAP_WIDTH) + 2);

    const std::vector<std::string> text = FragmentationReporter::comparisonLines("unit test", runs);
    std::string joined;
    for (std::size_t i = 0; i < text.size(); ++i) joined += text[i] + "\n";
    CHECK(contains(joined, "Fewest rejected VMs: best-fit"));
    CHECK(contains(joined, "first-fit  F(290 MB)"));

    // Generator: deterministic, well-formed, and replays without a single error.
    const std::vector<std::string> g1 = generateTrace(2000, 500, 7);
    const std::vector<std::string> g2 = generateTrace(2000, 500, 7);
    const std::vector<std::string> g3 = generateTrace(2000, 500, 8);
    CHECK(g1 == g2);
    CHECK(g1 != g3);
    CHECK(g1.size() == 502);  // header comment + HEAP + 500 operations
    CHECK(g1[1] == "HEAP 2000");
    int creates = 0;
    for (std::size_t i = 0; i < g1.size(); ++i) {
        if (g1[i].compare(0, 7, "CREATE ") == 0) ++creates;
    }
    CHECK(creates > 150 && creates < 350);
    const std::vector<StrategyRun> gr = StrategyComparator::run(g1, 1000, 0);
    for (std::size_t i = 0; i < gr.size(); ++i) {
        CHECK(gr[i].metrics.errors == 0);
        CHECK(gr[i].heapSize == 2000);
        CHECK(gr[i].metrics.placed + gr[i].metrics.rejected == creates);  // every CREATE is placed or rejected
        CHECK(gr[i].metrics.rejected == gr[i].metrics.rejectedFragmentation + gr[i].metrics.rejectedCapacity);
        CHECK(gr[i].metrics.rejectedVMs.size() == static_cast<std::size_t>(gr[i].metrics.rejected));
    }
    const std::vector<std::string> tiny = generateTrace(1, 20, 0);  // degenerate heap size must still work
    const std::vector<StrategyRun> tr = StrategyComparator::run(tiny, 1, 0);
    CHECK(tr[0].metrics.errors == 0 && tr[0].heapSize == 1);
}

static void testLog() {
    std::cout << "session log (fstream)\n";
    const std::string path = "heaptrace_unit_test.log";
    {
        SessionLog log;
        CHECK(!log.isOpen());
        log.write("ignored while closed");  // must not crash
        CHECK(log.open(path));
        CHECK(log.isOpen() && log.path() == path);
        std::ostringstream out;
        CommandProcessor p(100, "first", out, &log, ProcessorOptions());
        CHECK(run(p, "CREATE A 60") == CommandStatus::Ok);
        CHECK(run(p, "CREATE B 60") == CommandStatus::Rejected);
        CHECK(run(p, "NOPE") == CommandStatus::Error);
        p.finish();
        log.close();
        CHECK(!log.isOpen());
    }
    std::ifstream in(path.c_str());
    CHECK(in.is_open());
    std::string all, line;
    while (std::getline(in, line)) all += line + "\n";
    in.close();
    CHECK(contains(all, "[OK] CREATE A 60 MB -> placed at [0, 60)"));
    CHECK(contains(all, "[REJECTED] CREATE B 60 MB"));
    CHECK(contains(all, "[ERROR] line"));
    CHECK(contains(all, "=== Final heap state ==="));
    std::remove(path.c_str());

    SessionLog bad;
    CHECK(!bad.open("no_such_directory_for_heaptrace/x.log"));
    CHECK(!bad.isOpen());
    bad.write("still harmless");
}

static void testReporter() {
    std::cout << "reporter output\n";
    std::ostringstream out;
    CommandProcessor p(100, "first", out, 0, ProcessorOptions());
    run(p, "CREATE Alpha 25");
    run(p, "CREATE Beta 25");
    const std::vector<std::string> snap = FragmentationReporter::snapshotLines(p.heap(), p.vms());
    CHECK(snap.size() == 5);  // header + 3 blocks + map
    CHECK(snap[0] == "Heap snapshot [first-fit], total 100 MB");
    CHECK(contains(snap[1], "USED  Alpha (vm#0)"));
    CHECK(contains(snap[3], "FREE"));
    const std::string map = FragmentationReporter::memoryMap(p.heap(), p.vms(), 20);
    CHECK(map == "|AAAAABBBBB..........|");
    CHECK(FragmentationReporter::memoryMap(p.heap(), p.vms(), 1).size() == 3);
}

// The leak check is only meaningful if allocations made inside the C++ library
// (e.g. by std::string) go through our operator new too. That is true for static
// linking and for ELF interposition, but not for every platform, so verify it.
static bool allocationTrackerIsReliable() {
    const long before = g_outstanding;
    bool counted = false;
    {
        std::string s(200, 'x');
        volatile char touch = s[100];
        (void)touch;
        counted = (g_outstanding == before + 1);
    }
    return counted && g_outstanding == before;
}

int main() {
    const bool trackerReliable = allocationTrackerIsReliable();
    const long allocationsAtStart = g_outstanding;
    testUtil();
    testHeapBasics();
    testCoalescing();
    testStrategies();
    testVmManager();
    testWorkedExample();
    testProcessor();
    testStress();
    testComparatorAndGenerator();
    testLog();
    testReporter();

    std::cout << "\n" << g_checks << " checks, " << g_failures << " failed\n";
    if (MemoryBlock::liveCount() != 0) {
        std::cout << "LEAK: " << MemoryBlock::liveCount() << " MemoryBlock objects still alive\n";
        return 1;
    }
    if (!trackerReliable) {
        std::cout << "allocation tracking: skipped (not reliable with this C++ runtime; the MemoryBlock leak check still ran)\n";
    } else if (g_outstanding != allocationsAtStart) {
        std::cout << "LEAK: " << (g_outstanding - allocationsAtStart) << " allocations were never freed\n";
        return 1;
    } else {
        std::cout << "allocation tracking: every new was matched by a delete\n";
    }
    std::cout << (g_failures == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
