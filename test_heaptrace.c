/*
 * Self-contained unit/integration tests for the C HeapTrace implementation.
 * Build and run; exit code 0 means every check passed.
 */
#include "heaptrace.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            fprintf(stderr, "  FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b) CHECK(fabs((a) - (b)) < 1e-9)

static int contains(const char *haystack, const char *needle) { return strstr(haystack, needle) != NULL; }

/* Reads a whole text file into a malloc'ed string (NULL if it cannot be opened). */
static char *slurp(const char *path) {
    FILE *fp = fopen(path, "rb");
    char *buf;
    long n;
    if (fp == NULL) return NULL;
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = (char *)malloc((size_t)n + 1);
    if (buf != NULL) {
        size_t got = fread(buf, 1, (size_t)n, fp);
        buf[got] = '\0';
    }
    fclose(fp);
    return buf;
}

static void test_util(void) {
    MemSize v = 0;
    char buf[64];
    char longname[40];
    printf("util\n");
    CHECK(ht_parse_size("1", &v) && v == 1);
    CHECK(ht_parse_size("1000", &v) && v == 1000);
    CHECK(ht_parse_size("1000000000000", &v) && v == 1000000000000LL);
    CHECK(!ht_parse_size("1000000000001", &v));
    CHECK(!ht_parse_size("0", &v));
    CHECK(!ht_parse_size("-1", &v));
    CHECK(!ht_parse_size("", &v));
    CHECK(!ht_parse_size("12a", &v));
    CHECK(!ht_parse_size("1.5", &v));
    CHECK(!ht_parse_size(" 5", &v));
    CHECK(!ht_parse_size("99999999999999999999", &v)); /* would overflow without the length guard */
    CHECK(ht_valid_vm_name("A"));
    CHECK(ht_valid_vm_name("web-1.prod_x"));
    CHECK(ht_valid_vm_name("_x"));
    CHECK(!ht_valid_vm_name(""));
    CHECK(!ht_valid_vm_name("-x"));
    CHECK(!ht_valid_vm_name(".x"));
    CHECK(!ht_valid_vm_name("a b"));
    CHECK(!ht_valid_vm_name("a#b"));
    memset(longname, 'a', 33);
    longname[33] = '\0';
    CHECK(!ht_valid_vm_name(longname));
    longname[32] = '\0';
    CHECK(ht_valid_vm_name(longname));

    strcpy(buf, "  \t x y \r\n");
    CHECK(strcmp(ht_trim(buf), "x y") == 0);
    strcpy(buf, "cReate");
    ht_to_upper(buf);
    CHECK(strcmp(buf, "CREATE") == 0);
    strcpy(buf, "BeSt-Fit");
    ht_to_lower(buf);
    CHECK(strcmp(buf, "best-fit") == 0);
}

static void test_heap_basics(void) {
    const long base = ht_live_blocks();
    Heap *h;
    HeapStats s;
    AllocationResult r;
    printf("heap basics: split, exact fit, rejection reasons, invalid requests\n");

    h = heap_create(100);
    CHECK(h != NULL && heap_validate(h, NULL, 0));
    s = heap_stats(h);
    CHECK(s.total == 100 && s.free_blocks == 1 && s.largest_free == 100 && s.used_blocks == 0);
    CHECK_NEAR(s.external_fragmentation, 0.0);

    r = heap_allocate(h, 0, 30);
    CHECK(r.success && r.start == 0 && r.reason == HT_REJECT_NONE);
    s = heap_stats(h);
    CHECK(s.used == 30 && s.free_memory == 70 && s.free_blocks == 1 && s.used_blocks == 1);
    CHECK(heap_validate(h, NULL, 0));

    r = heap_allocate(h, 1, 70); /* exact fit: no split, no leftover block */
    CHECK(r.success && r.start == 30);
    s = heap_stats(h);
    CHECK(s.free_blocks == 0 && s.free_memory == 0 && s.largest_free == 0);
    CHECK_NEAR(s.external_fragmentation, 0.0); /* nothing free: defined as 0, not NaN */
    CHECK(heap_validate(h, NULL, 0));

    r = heap_allocate(h, 2, 1);
    CHECK(!r.success && r.reason == HT_REJECT_CAPACITY && r.start == -1);
    CHECK(heap_allocate(h, -1, 5).reason == HT_REJECT_INVALID);
    CHECK(heap_allocate(h, 5, 0).reason == HT_REJECT_INVALID);
    CHECK(heap_allocate(h, 5, -3).reason == HT_REJECT_INVALID);
    CHECK(heap_allocate(h, 0, 1).reason == HT_REJECT_INVALID); /* owner 0 already holds a block */
    CHECK(heap_validate(h, NULL, 0));

    CHECK(!heap_release(h, 99).found);
    CHECK(!heap_release(h, -1).found);
    CHECK(heap_block_at(h, 0) != NULL && heap_block_at(h, 99) != NULL);
    CHECK(heap_block_at(h, -1) == NULL && heap_block_at(h, 100) == NULL);
    heap_destroy(h);
    CHECK(ht_live_blocks() == base); /* destroy freed every node */

    CHECK(heap_create(0) == NULL);
    CHECK(heap_create(-5) == NULL);
    CHECK(heap_create(HT_MAX_MEMORY_MB + 1) == NULL);
    CHECK(ht_live_blocks() == base); /* a failed create leaks nothing */
}

static void test_coalescing(void) {
    const long base = ht_live_blocks();
    Heap *h;
    ReleaseResult r;
    int i;
    printf("coalescing: left, right, both, none\n");

    h = heap_create(40);
    for (i = 0; i < 4; ++i) CHECK(heap_allocate(h, i, 10).success); /* A B C D */

    r = heap_release(h, 1); /* B: neighbours A and C are used -> no merge */
    CHECK(r.found && r.merges == 0 && r.start == 10 && r.size == 10);
    CHECK(heap_stats(h).free_blocks == 1 && heap_validate(h, NULL, 0));

    r = heap_release(h, 3); /* D: last block, previous neighbour C is used -> no merge */
    CHECK(r.found && r.merges == 0);
    CHECK(heap_stats(h).free_blocks == 2 && heap_validate(h, NULL, 0));

    r = heap_release(h, 2); /* C: free neighbours on both sides -> merges twice */
    CHECK(r.found && r.merges == 2);
    CHECK(heap_stats(h).free_blocks == 1 && heap_stats(h).largest_free == 30 && heap_validate(h, NULL, 0));

    r = heap_release(h, 0); /* A: merges with the free block on its right */
    CHECK(r.found && r.merges == 1);
    CHECK(heap_stats(h).free_blocks == 1 && heap_stats(h).largest_free == 40 && h->head->is_free &&
          h->head->size == 40 && h->head->next == NULL);
    CHECK(heap_validate(h, NULL, 0));
    CHECK(!heap_release(h, 0).found); /* double release is rejected, not corrupting */
    heap_destroy(h);

    h = heap_create(30);
    for (i = 0; i < 3; ++i) CHECK(heap_allocate(h, i, 10).success);
    CHECK(heap_release(h, 0).merges == 0);
    CHECK(heap_release(h, 1).merges == 1); /* merges with the free block on its LEFT */
    CHECK(heap_release(h, 2).merges == 1);
    CHECK(heap_stats(h).free_blocks == 1 && heap_stats(h).largest_free == 30 && heap_validate(h, NULL, 0));
    heap_destroy(h);
    CHECK(ht_live_blocks() == base);
}

/* Layout used by the strategy tests: free holes of 100 @0, 200 @150, 300 @400, 250 @750. */
static void build_holes(Heap *h) {
    static const MemSize sizes[6] = {100, 50, 200, 50, 300, 50};
    int i;
    for (i = 0; i < 6; ++i) CHECK(heap_allocate(h, i, sizes[i]).success);
    CHECK(heap_release(h, 0).found);
    CHECK(heap_release(h, 2).found);
    CHECK(heap_release(h, 4).found);
    CHECK(heap_stats(h).free_blocks == 4 && heap_stats(h).free_memory == 850);
}

static MemSize place_with(const char *strategy, MemSize request) {
    Heap *h = heap_create(1000);
    AllocationResult r;
    build_holes(h);
    heap_set_strategy(h, ht_strategy_by_name(strategy));
    r = heap_allocate(h, 100, request);
    CHECK(heap_validate(h, NULL, 0));
    heap_destroy(h);
    return r.success ? r.start : -1;
}

static void test_strategies(void) {
    Heap *h;
    AllocationResult r;
    printf("strategies: first / best / worst, ties, run-time switching\n");
    CHECK(ht_strategy_by_name("first") && ht_strategy_by_name("FIRST-FIT") && ht_strategy_by_name("firstfit"));
    CHECK(ht_strategy_by_name("Best") && ht_strategy_by_name("best-fit"));
    CHECK(ht_strategy_by_name("worst") && ht_strategy_by_name("worst-fit"));
    CHECK(!ht_strategy_by_name("") && !ht_strategy_by_name("next-fit") && !ht_strategy_by_name("fit"));
    CHECK(!ht_strategy_by_name(NULL));
    CHECK(strcmp(ht_strategy_by_name("best")->name, "best-fit") == 0);

    /* request 90: holes 100@0 200@150 300@400 250@750 */
    CHECK(place_with("first", 90) == 0);
    CHECK(place_with("best", 90) == 0);
    CHECK(place_with("worst", 90) == 400);
    CHECK(place_with("first", 150) == 150);
    CHECK(place_with("best", 150) == 150);
    CHECK(place_with("worst", 150) == 400);
    /* request 220: first-fit takes the 300 hole, best-fit the 250 hole */
    CHECK(place_with("first", 220) == 400);
    CHECK(place_with("best", 220) == 750);
    CHECK(place_with("worst", 220) == 400);
    /* 301 > largest hole (300) although 850 MB are free */
    CHECK(place_with("first", 301) == -1);
    CHECK(place_with("best", 301) == -1);
    CHECK(place_with("worst", 301) == -1);

    h = heap_create(1000); /* fragmentation vs capacity reasons */
    build_holes(h);
    r = heap_allocate(h, 100, 301);
    CHECK(!r.success && r.reason == HT_REJECT_FRAGMENTATION);
    r = heap_allocate(h, 100, 851);
    CHECK(!r.success && r.reason == HT_REJECT_CAPACITY);
    CHECK(heap_validate(h, NULL, 0));
    heap_destroy(h);

    h = heap_create(100); /* ties: lowest address wins for best-fit and worst-fit */
    CHECK(heap_allocate(h, 0, 10).success);
    CHECK(heap_allocate(h, 1, 20).success);
    CHECK(heap_allocate(h, 2, 10).success);
    CHECK(heap_allocate(h, 3, 20).success);
    CHECK(heap_allocate(h, 4, 40).success);
    CHECK(heap_release(h, 1).found); /* hole 20 @10 */
    CHECK(heap_release(h, 3).found); /* hole 20 @40 */
    heap_set_strategy(h, &HT_BEST_FIT);
    CHECK(heap_allocate(h, 5, 20).start == 10);
    CHECK(heap_release(h, 5).found);
    heap_set_strategy(h, &HT_WORST_FIT);
    CHECK(heap_allocate(h, 6, 5).start == 10);
    CHECK(heap_validate(h, NULL, 0));
    heap_destroy(h);

    h = heap_create(100); /* run-time switch keeps the heap intact; NULL is ignored */
    CHECK(strcmp(h->strategy->name, "first-fit") == 0);
    heap_set_strategy(h, &HT_BEST_FIT);
    CHECK(strcmp(h->strategy->name, "best-fit") == 0);
    heap_set_strategy(h, NULL);
    CHECK(strcmp(h->strategy->name, "best-fit") == 0);
    CHECK(heap_allocate(h, 0, 10).success && heap_validate(h, NULL, 0));
    heap_destroy(h);
}

static void test_vm_manager(void) {
    Heap *h = heap_create(100);
    VMManager *m = vm_manager_create(h);
    CreateResult a, dup, big, again;
    TerminateResult t;
    printf("VM manager\n");

    a = vm_create(m, "A", 40);
    CHECK(!a.duplicate && !a.failed && a.id == 0 && a.alloc.success && a.alloc.start == 0);
    dup = vm_create(m, "A", 10);
    CHECK(dup.duplicate && dup.id == -1);
    CHECK(m->count == 1);

    big = vm_create(m, "BIG", 200);
    CHECK(!big.duplicate && big.id == 1 && !big.alloc.success && big.alloc.reason == HT_REJECT_CAPACITY);
    CHECK(vm_find_by_id(m, 1) != NULL && vm_find_by_id(m, 1)->state == VM_REJECTED);
    CHECK(vm_running_count(m) == 1);

    t = vm_terminate(m, "BIG");
    CHECK(t.status == TERM_WAS_REJECTED);
    CHECK(vm_terminate(m, "nobody").status == TERM_NOT_FOUND);

    t = vm_terminate(m, "A");
    CHECK(t.status == TERM_TERMINATED && t.release.found && t.release.size == 40);
    CHECK(vm_terminate(m, "A").status == TERM_NOT_FOUND); /* already terminated */
    CHECK(vm_find_by_id(m, 0)->state == VM_TERMINATED);
    CHECK(vm_running_count(m) == 0);

    again = vm_create(m, "A", 60); /* a terminated name may be reused, with a new id */
    CHECK(!again.duplicate && again.id == 2 && again.alloc.success);
    CHECK(vm_find_by_id(m, 2)->state == VM_RUNNING && vm_find_by_id(m, 2)->start == again.alloc.start);
    CHECK(vm_find_by_id(m, -1) == NULL && vm_find_by_id(m, 99) == NULL);
    CHECK(heap_validate(h, NULL, 0));

    { /* the VM table grows past its initial capacity without losing records */
        Heap *h2 = heap_create(100000);
        VMManager *m2 = vm_manager_create(h2);
        char name[16];
        int i;
        for (i = 0; i < 100; ++i) {
            sprintf(name, "vm%d", i);
            CHECK(vm_create(m2, name, 10).alloc.success);
        }
        CHECK(m2->count == 100 && vm_running_count(m2) == 100);
        CHECK(strcmp(vm_find_by_id(m2, 99)->name, "vm99") == 0 && vm_find_by_id(m2, 0)->start == 0);
        CHECK(heap_validate(h2, NULL, 0));
        vm_manager_destroy(m2);
        heap_destroy(h2);
    }
    vm_manager_destroy(m);
    heap_destroy(h);
}

static CommandStatus exec(Processor *p, const char *line) {
    static int n = 0;
    return processor_execute(p, line, ++n);
}

static void test_worked_example(void) {
    static const char *const setup[] = {"HEAP 1000",    "CREATE A 300", "CREATE B 100", "CREATE C 200",
                                        "CREATE D 150", "TERMINATE A",  "TERMINATE C",  "CREATE E 180"};
    int pass;
    size_t i;
    printf("worked example from the presentation (1000 MB server)\n");
    for (pass = 0; pass < 2; ++pass) {
        const int best = (pass == 1);
        Processor *p = processor_create(1000, best ? "best" : "first", NULL, NULL, processor_default_options());
        HeapStats s;
        char line[512];
        CHECK(p != NULL);
        for (i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) CHECK(exec(p, setup[i]) == CMD_OK);

        s = heap_stats(p->heap);
        CHECK(s.free_blocks == 3 && s.free_memory == 570 && s.used == 430);
        report_stats_line(&s, line, sizeof(line));
        if (!best) {
            CHECK(s.largest_free == 250);
            CHECK_NEAR(s.external_fragmentation, 1.0 - 250.0 / 570.0);
            CHECK(contains(line, "external fragmentation=56.1%"));
            CHECK(exec(p, "CREATE F 290") == CMD_REJECTED); /* 570 MB free, largest hole 250 */
            CHECK(p->metrics.rejected_fragmentation == 1 && p->metrics.rejected_capacity == 0);
            CHECK(vm_find_by_id(p->vms, 4)->start == 0); /* E went into the first (300 MB) hole */
        } else {
            CHECK(s.largest_free == 300);
            CHECK_NEAR(s.external_fragmentation, 1.0 - 300.0 / 570.0);
            CHECK(contains(line, "external fragmentation=47.4%"));
            CHECK(vm_find_by_id(p->vms, 4)->start == 400); /* E went into the tightest (200 MB) hole */
            CHECK(exec(p, "CREATE F 290") == CMD_OK);
            CHECK(vm_find_by_id(p->vms, 5)->start == 0);
        }
        CHECK(heap_validate(p->heap, NULL, 0));
        processor_destroy(p);
    }
}

static void test_processor(void) {
    static const char *const bad[] = {"CREATE",      "CREATE X",   "CREATE X 10 20", "CREATE X 0",
                                      "CREATE X -1", "CREATE X abc", "CREATE X 1e3",  "CREATE -X 5",
                                      "CREATE A 5",  "TERMINATE",  "TERMINATE Z",    "TERMINATE A B",
                                      "HEAP",        "HEAP 0",     "HEAP -5",        "HEAP 10x",
                                      "STRATEGY",    "STRATEGY foo", "STRATEGY best extra", "BOGUS",
                                      "SNAPSHOT now", "QUIT now"};
    const int nbad = (int)(sizeof(bad) / sizeof(bad[0]));
    Processor *p = processor_create(500, "first", NULL, NULL, processor_default_options());
    Processor *q;
    ProcessorOptions locked;
    char before[512], after[512];
    HeapStats s;
    int i;
    printf("command processor: errors leave the heap unchanged\n");

    CHECK(exec(p, "CREATE A 100") == CMD_OK);
    s = heap_stats(p->heap);
    report_stats_line(&s, before, sizeof(before));
    for (i = 0; i < nbad; ++i) CHECK(exec(p, bad[i]) == CMD_ERROR);
    CHECK(p->metrics.errors == nbad);
    s = heap_stats(p->heap);
    report_stats_line(&s, after, sizeof(after));
    CHECK(strcmp(before, after) == 0);
    CHECK(heap_validate(p->heap, NULL, 0));

    CHECK(exec(p, "") == CMD_IGNORED);
    CHECK(exec(p, "   \t ") == CMD_IGNORED);
    CHECK(exec(p, "# only a comment") == CMD_IGNORED);
    CHECK(exec(p, "create B 50   # trailing comment") == CMD_OK);
    CHECK(exec(p, "CREATE C 20\r") == CMD_OK); /* CRLF line endings */
    CHECK(exec(p, "  terminate   B  ") == CMD_OK);
    CHECK(exec(p, "SNAPSHOT") == CMD_OK);
    CHECK(exec(p, "stats") == CMD_OK);
    CHECK(exec(p, "help") == CMD_OK);
    CHECK(exec(p, "STRATEGY WORST") == CMD_OK);
    CHECK(strcmp(p->heap->strategy->name, "worst-fit") == 0);
    CHECK(exec(p, "HEAP 80") == CMD_OK); /* re-initialise keeps the strategy */
    CHECK(p->heap->total == 80 && heap_stats(p->heap).free_blocks == 1 && strcmp(p->heap->strategy->name, "worst-fit") == 0);
    CHECK(p->vms->count == 0);
    CHECK(exec(p, "CREATE A 80") == CMD_OK); /* name A is free again after the reset */
    CHECK(exec(p, "COMPARE") == CMD_OK);
    CHECK(exec(p, "quit") == CMD_QUIT);
    CHECK(exec(p, "exit") == CMD_QUIT);
    processor_destroy(p);

    /* Locked mode (used for replays) ignores STRATEGY and COMPARE and is silent. */
    locked = processor_default_options();
    locked.print_ops = 0;
    locked.lock_strategy = 1;
    q = processor_create(100, "best", NULL, NULL, locked);
    CHECK(exec(q, "STRATEGY first") == CMD_IGNORED);
    CHECK(exec(q, "COMPARE") == CMD_IGNORED);
    CHECK(strcmp(q->heap->strategy->name, "best-fit") == 0);
    CHECK(exec(q, "CREATE A 10") == CMD_OK);
    processor_destroy(q);

    CHECK(processor_create(100, "nonsense", NULL, NULL, processor_default_options()) == NULL);
    CHECK(processor_create(0, "first", NULL, NULL, processor_default_options()) == NULL);
}

/* Output capture: run commands with a real FILE and read it back. */
static void test_output_capture(void) {
    const char *path = "heaptrace_unit_test_out.txt";
    FILE *fp = fopen(path, "w");
    ProcessorOptions locked;
    Processor *p;
    char *text;
    printf("console output\n");
    CHECK(fp != NULL);
    p = processor_create(100, "first", fp, NULL, processor_default_options());
    exec(p, "CREATE A 60");
    exec(p, "CREATE B 60");
    exec(p, "NOPE");
    exec(p, "COMPARE");
    processor_destroy(p);
    fclose(fp);
    text = slurp(path);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(contains(text, "[OK]       CREATE A 60 MB -> placed at [0, 60)"));
        CHECK(contains(text, "[REJECTED] CREATE B 60 MB -> only 40 MB free in total (insufficient memory)"));
        CHECK(contains(text, "[ERROR]    line "));
        CHECK(contains(text, "=== Strategy comparison: current session (2 commands) ==="));
    }
    free(text);

    fp = fopen(path, "w"); /* a silent (locked, print_ops = 0) processor writes nothing */
    locked = processor_default_options();
    locked.print_ops = 0;
    locked.print_stats = 0;
    locked.lock_strategy = 1;
    p = processor_create(100, "first", fp, NULL, locked);
    exec(p, "CREATE A 10");
    exec(p, "TERMINATE A");
    exec(p, "NOPE");
    processor_destroy(p);
    fclose(fp);
    text = slurp(path);
    CHECK(text != NULL && text[0] == '\0');
    free(text);
    remove(path);
}

/* Deterministic generator for the stress test (independent of tracegen.c). */
static unsigned long long g_state = 12345;
static unsigned long long next_random(void) {
    g_state = g_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return g_state >> 33;
}

#define STRESS_MAX_IDS 40000

static void test_stress(void) {
    static const char *const names[3] = {"first", "best", "worst"};
    static MemSize live_size[STRESS_MAX_IDS]; /* the model: owner id -> size, 0 = not live */
    const long base = ht_live_blocks();
    int s;
    printf("randomised stress test with invariant checks after every operation\n");
    for (s = 0; s < 3; ++s) {
        Heap *h = heap_create(5000);
        int next_id = 0, step, ok = 1, id;
        MemSize model_used = 0;
        g_state = 987654321ULL + (unsigned long long)s;
        memset(live_size, 0, sizeof(live_size));
        heap_set_strategy(h, ht_strategy_by_name(names[s]));

        for (step = 0; step < 30000 && ok; ++step) {
            int live_count = 0;
            HeapStats st;
            if (model_used == 0 || next_random() % 100 < 55) {
                MemSize size = 1 + (MemSize)(next_random() % 700);
                AllocationResult r = heap_allocate(h, next_id, size);
                if (r.success) {
                    const MemoryBlock *b = heap_block_at(h, r.start);
                    live_size[next_id] = size;
                    model_used += size;
                    ok = ok && b != NULL && !b->is_free && b->owner_id == next_id && b->start == r.start && b->size == size;
                } else {
                    st = heap_stats(h);
                    ok = ok && r.reason == (st.free_memory >= size ? HT_REJECT_FRAGMENTATION : HT_REJECT_CAPACITY);
                    ok = ok && st.largest_free < size; /* it really did not fit anywhere */
                }
                ++next_id;
            } else {
                int target, seen = 0;
                for (id = 0; id < next_id; ++id) live_count += live_size[id] > 0;
                target = (int)(next_random() % (unsigned long long)live_count);
                for (id = 0; id < next_id; ++id) {
                    if (live_size[id] > 0 && seen++ == target) break;
                }
                {
                    ReleaseResult r = heap_release(h, id);
                    ok = ok && r.found && r.size == live_size[id];
                    model_used -= live_size[id];
                    live_size[id] = 0;
                }
            }
            st = heap_stats(h);
            ok = ok && heap_validate(h, NULL, 0) && st.used == model_used && st.used + st.free_memory == 5000 &&
                 st.external_fragmentation >= 0.0 && st.external_fragmentation <= 1.0;
        }
        CHECK(ok);
        for (id = 0; id < next_id; ++id) {
            if (live_size[id] > 0) CHECK(heap_release(h, id).found); /* drain */
        }
        {
            HeapStats st = heap_stats(h);
            CHECK(st.free_blocks == 1 && st.largest_free == 5000 && st.used_blocks == 0 && heap_validate(h, NULL, 0));
        }
        heap_destroy(h);
        CHECK(ht_live_blocks() == base);
    }
}

static void test_comparator_and_generator(void) {
    static const char *const lines[] = {"HEAP 1000",    "CREATE A 300", "CREATE B 100",   "CREATE C 200",
                                        "CREATE D 150", "TERMINATE A",  "TERMINATE C",    "STRATEGY best",
                                        "CREATE E 180", "CREATE F 290", "COMPARE"};
    StrList trace, text, g1, g2, g3, tiny;
    StrategyRun runs[3], gr[3], tr[3];
    size_t i;
    int creates = 0, k;
    int joined_ok;
    StrBuf joined;
    printf("comparator and trace generator\n");

    memset(&trace, 0, sizeof(trace));
    for (i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) strlist_push(&trace, lines[i]);
    CHECK(compare_run(&trace, 1000, NULL, runs));
    CHECK(strcmp(runs[0].strategy, "first-fit") == 0 && strcmp(runs[1].strategy, "best-fit") == 0 &&
          strcmp(runs[2].strategy, "worst-fit") == 0);
    CHECK(runs[0].metrics.rejected == 1 && runs[0].metrics.rejected_vms.count == 1 &&
          strcmp(runs[0].metrics.rejected_vms.items[0], "F(290 MB)") == 0);
    CHECK(runs[1].metrics.rejected == 0 && runs[1].metrics.placed == 6);
    CHECK(runs[2].metrics.rejected == 1);
    CHECK(runs[0].metrics.errors == 0 && runs[1].metrics.errors == 0 && runs[2].metrics.errors == 0); /* STRATEGY/COMPARE ignored */
    CHECK(runs[0].final_stats.free_blocks == 3 && runs[0].final_stats.largest_free == 250);
    CHECK(runs[1].heap_size == 1000);
    CHECK(strlen(runs[0].final_map) == (size_t)HT_MAP_WIDTH + 2);

    memset(&text, 0, sizeof(text));
    CHECK(report_comparison_lines("unit test", runs, 3, &text));
    memset(&joined, 0, sizeof(joined));
    joined_ok = 1;
    for (i = 0; i < text.count; ++i) {
        joined_ok = joined_ok && strbuf_append(&joined, text.items[i]) && strbuf_append(&joined, "\n");
    }
    CHECK(joined_ok && contains(joined.data, "Fewest rejected VMs: best-fit"));
    CHECK(contains(joined.data, "first-fit  F(290 MB)"));
    strbuf_free(&joined);
    strlist_free(&text);
    for (k = 0; k < 3; ++k) strategy_run_free(&runs[k]);
    strlist_free(&trace);

    /* Generator: deterministic, well-formed, and replays without a single error. */
    memset(&g1, 0, sizeof(g1));
    memset(&g2, 0, sizeof(g2));
    memset(&g3, 0, sizeof(g3));
    CHECK(generate_trace(2000, 500, 7, &g1) && generate_trace(2000, 500, 7, &g2) && generate_trace(2000, 500, 8, &g3));
    CHECK(g1.count == 502 && g2.count == 502 && g3.count == 502);
    {
        int same = 1, different = 0;
        for (i = 0; i < g1.count; ++i) {
            if (strcmp(g1.items[i], g2.items[i]) != 0) same = 0;
            if (strcmp(g1.items[i], g3.items[i]) != 0) different = 1;
        }
        CHECK(same);
        CHECK(different);
    }
    CHECK(strcmp(g1.items[1], "HEAP 2000") == 0);
    for (i = 0; i < g1.count; ++i) {
        if (strncmp(g1.items[i], "CREATE ", 7) == 0) ++creates;
    }
    CHECK(creates > 150 && creates < 350);
    CHECK(compare_run(&g1, 1000, NULL, gr));
    for (k = 0; k < 3; ++k) {
        CHECK(gr[k].metrics.errors == 0);
        CHECK(gr[k].heap_size == 2000);
        CHECK(gr[k].metrics.placed + gr[k].metrics.rejected == creates);
        CHECK(gr[k].metrics.rejected == gr[k].metrics.rejected_fragmentation + gr[k].metrics.rejected_capacity);
        CHECK(gr[k].metrics.rejected_vms.count == (size_t)gr[k].metrics.rejected);
        strategy_run_free(&gr[k]);
    }
    memset(&tiny, 0, sizeof(tiny)); /* degenerate heap size must still work */
    CHECK(generate_trace(1, 20, 0, &tiny));
    CHECK(compare_run(&tiny, 1, NULL, tr));
    CHECK(tr[0].metrics.errors == 0 && tr[0].heap_size == 1);
    for (k = 0; k < 3; ++k) strategy_run_free(&tr[k]);
    strlist_free(&tiny);
    strlist_free(&g1);
    strlist_free(&g2);
    strlist_free(&g3);
}

static void test_log(void) {
    const char *path = "heaptrace_unit_test.log";
    SessionLog log;
    SessionLog bad;
    Processor *p;
    char *all;
    printf("session log (file handling)\n");

    memset(&log, 0, sizeof(log));
    log_write(&log, "ignored while closed"); /* must not crash */
    CHECK(log_open(&log, path));
    CHECK(log.fp != NULL && strcmp(log.path, path) == 0);
    p = processor_create(100, "first", NULL, &log, processor_default_options());
    CHECK(exec(p, "CREATE A 60") == CMD_OK);
    CHECK(exec(p, "CREATE B 60") == CMD_REJECTED);
    CHECK(exec(p, "NOPE") == CMD_ERROR);
    processor_finish(p);
    processor_destroy(p);
    log_close(&log);
    CHECK(log.fp == NULL);

    all = slurp(path);
    CHECK(all != NULL);
    if (all != NULL) {
        CHECK(contains(all, "[OK] CREATE A 60 MB -> placed at [0, 60)"));
        CHECK(contains(all, "[REJECTED] CREATE B 60 MB"));
        CHECK(contains(all, "[ERROR] line"));
        CHECK(contains(all, "=== Final heap state ==="));
    }
    free(all);
    remove(path);

    memset(&bad, 0, sizeof(bad));
    CHECK(!log_open(&bad, "no_such_directory_for_heaptrace/x.log"));
    CHECK(bad.fp == NULL);
    log_write(&bad, "still harmless");
    log_close(&bad);
}

static void test_reporter(void) {
    Processor *p = processor_create(100, "first", NULL, NULL, processor_default_options());
    StrList snap;
    char map[HT_MAP_WIDTH + 3];
    printf("reporter output\n");

    exec(p, "CREATE Alpha 25");
    exec(p, "CREATE Beta 25");
    memset(&snap, 0, sizeof(snap));
    CHECK(report_snapshot_lines(p->heap, p->vms, &snap));
    CHECK(snap.count == 5); /* header + 3 blocks + map */
    CHECK(strcmp(snap.items[0], "Heap snapshot [first-fit], total 100 MB") == 0);
    CHECK(contains(snap.items[1], "USED  Alpha (vm#0)"));
    CHECK(contains(snap.items[3], "FREE"));
    report_memory_map(p->heap, p->vms, 20, map, sizeof(map));
    CHECK(strcmp(map, "|AAAAABBBBB..........|") == 0);
    report_memory_map(p->heap, p->vms, 1, map, sizeof(map));
    CHECK(strlen(map) == 3);
    strlist_free(&snap);
    processor_destroy(p);
}

int main(void) {
    test_util();
    test_heap_basics();
    test_coalescing();
    test_strategies();
    test_vm_manager();
    test_worked_example();
    test_processor();
    test_output_capture();
    test_stress();
    test_comparator_and_generator();
    test_log();
    test_reporter();

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    if (ht_live_blocks() != 0) {
        printf("LEAK: %ld MemoryBlock nodes still allocated\n", ht_live_blocks());
        return 1;
    }
#ifdef HT_ALLOC_TRACKING
    if (ht_track_outstanding() != 0) {
        printf("LEAK: %ld heap allocations were never freed\n", ht_track_outstanding());
        return 1;
    }
    printf("allocation tracking: every malloc/calloc/realloc was matched by a free\n");
#endif
    printf(g_failures == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
