/*
 * heaptrace.h - HeapTrace, C implementation.
 *
 * Same design as the C++ version, expressed in C99:
 *   - MemoryBlock : node of a hand-written doubly linked heap list
 *   - Strategy    : "virtual table" of function pointers (first/best/worst-fit),
 *                   the C counterpart of the AllocationStrategy class hierarchy
 *   - Heap        : allocation engine (split on allocate, coalesce on release)
 *   - VMManager   : VM create / terminate lifecycle on top of the heap
 *   - Processor   : command interpreter shared by trace files and the console
 *   - report_*    : snapshots and fragmentation statistics
 *   - compare_run : replays one trace under all strategies
 *
 * Every function that allocates documents who must free the result.
 */
#ifndef HEAPTRACE_H
#define HEAPTRACE_H

#if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1 /* make %lld / %.1f behave identically on MinGW */
#endif

#include <stddef.h>
#include <stdio.h>

typedef long long MemSize; /* all sizes and addresses are whole MB */

#define HT_MAX_MEMORY_MB 1000000000000LL /* 10^12 MB: keeps every sum far below overflow */
#define HT_NO_OWNER (-1)
#define HT_MAP_WIDTH 60
#define HT_MAX_NAME 32

/* ------------------------------------------------------------------ util.c */

/* Growable list of owned strings. Zero-initialise before use. */
typedef struct {
    char **items;
    size_t count;
    size_t cap;
} StrList;

/* Growable string buffer. Zero-initialise before use. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

#if defined(__GNUC__)
#define HT_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define HT_PRINTF(a, b)
#endif

/* printf into a freshly malloc'ed string (NULL if out of memory). Caller frees. */
char *ht_asprintf(const char *fmt, ...) HT_PRINTF(1, 2);
char *ht_strdup(const char *s);

int strlist_push(StrList *l, const char *s);                    /* copies s; 0 on out-of-memory */
int strlist_pushf(StrList *l, const char *fmt, ...) HT_PRINTF(2, 3);
void strlist_clear(StrList *l);                                 /* frees items, keeps the list usable */
void strlist_free(StrList *l);                                  /* same as clear */

int strbuf_append(StrBuf *b, const char *s);
void strbuf_free(StrBuf *b);

void ht_to_upper(char *s);
void ht_to_lower(char *s);
char *ht_trim(char *s); /* trims in place, returns pointer into s */

/* Digits only, 1..18 characters, value in [lo, hi]. */
int ht_parse_integer(const char *token, long long lo, long long hi, long long *out);
int ht_parse_size(const char *token, MemSize *out); /* whole MB in [1, HT_MAX_MEMORY_MB] */
/* 1-32 chars from [A-Za-z0-9_.-], first char alphanumeric or '_'. */
int ht_valid_vm_name(const char *name);

/* ------------------------------------------------------------------ heap.c */

typedef struct MemoryBlock {
    MemSize start;
    MemSize size;
    int is_free;
    int owner_id;
    struct MemoryBlock *next;
    struct MemoryBlock *prev;
} MemoryBlock;

typedef struct Strategy {
    const char *name;
    /* Returns the free block that should serve `size` MB, or NULL. Must not modify the list. */
    MemoryBlock *(*select_block)(MemoryBlock *head, MemSize size);
} Strategy;

extern const Strategy HT_FIRST_FIT;
extern const Strategy HT_BEST_FIT;
extern const Strategy HT_WORST_FIT;

/* first | first-fit | best | best-fit | worst | worst-fit (any case); NULL if unknown. */
const Strategy *ht_strategy_by_name(const char *name);

typedef enum {
    HT_REJECT_NONE,
    HT_REJECT_FRAGMENTATION, /* enough free in total, but no single block large enough */
    HT_REJECT_CAPACITY,      /* less free in total than requested */
    HT_REJECT_INVALID        /* bad owner id/size, duplicate owner, or out of memory */
} RejectReason;

typedef struct {
    int success;
    MemSize start; /* -1 if rejected */
    RejectReason reason;
} AllocationResult;

typedef struct {
    int found;
    MemSize start;
    MemSize size;
    int merges; /* free neighbours merged: 0, 1 or 2 */
} ReleaseResult;

typedef struct {
    MemSize total;
    MemSize used;
    MemSize free_memory;
    int used_blocks;
    int free_blocks;
    MemSize largest_free;
    double external_fragmentation; /* 1 - largest_free / free_memory, 0 if nothing is free */
} HeapStats;

typedef struct {
    MemSize total;
    MemoryBlock *head;
    const Strategy *strategy;
} Heap;

/* NULL if total is outside [1, HT_MAX_MEMORY_MB] or memory is exhausted. */
Heap *heap_create(MemSize total);
void heap_destroy(Heap *h);
void heap_set_strategy(Heap *h, const Strategy *s); /* NULL is ignored */
AllocationResult heap_allocate(Heap *h, int owner_id, MemSize size);
ReleaseResult heap_release(Heap *h, int owner_id);
HeapStats heap_stats(const Heap *h);
const MemoryBlock *heap_block_at(const Heap *h, MemSize pos);
/* 1 if every structural invariant holds; otherwise 0 and a message in err (if non-NULL). */
int heap_validate(const Heap *h, char *err, size_t err_len);
/* Number of MemoryBlock nodes currently allocated (leak check for the tests). */
long ht_live_blocks(void);

/* -------------------------------------------------------------------- vm.c */

typedef enum { VM_RUNNING, VM_TERMINATED, VM_REJECTED } VMState;

typedef struct {
    int id;
    char name[HT_MAX_NAME + 1];
    MemSize memory;
    VMState state;
    MemSize start; /* valid while running */
} VirtualMachine;

typedef struct {
    Heap *heap; /* not owned */
    VirtualMachine *vms; /* index == VM id */
    size_t count;
    size_t cap;
} VMManager;

typedef struct {
    int duplicate; /* a VM with this name is already running; nothing was done */
    int failed;    /* internal failure (out of memory / invalid name length) */
    int id;        /* -1 if duplicate or failed */
    AllocationResult alloc;
} CreateResult;

typedef enum { TERM_TERMINATED, TERM_WAS_REJECTED, TERM_NOT_FOUND } TerminateStatus;

typedef struct {
    TerminateStatus status;
    int id;
    ReleaseResult release;
} TerminateResult;

VMManager *vm_manager_create(Heap *heap); /* the heap must outlive the manager */
void vm_manager_destroy(VMManager *m);
CreateResult vm_create(VMManager *m, const char *name, MemSize memory);
TerminateResult vm_terminate(VMManager *m, const char *name);
const VirtualMachine *vm_find_by_id(const VMManager *m, int id);
int vm_running_count(const VMManager *m);

/* -------------------------------------------------------------- session log */

typedef struct {
    FILE *fp;
    char *path;
} SessionLog;

int log_open(SessionLog *log, const char *path); /* truncates; 0 if it cannot be opened */
void log_write(SessionLog *log, const char *line); /* appends and flushes; no-op if closed */
void log_close(SessionLog *log);

/* ---------------------------------------------------------------- metrics */

typedef struct {
    int placed;
    int rejected;
    int rejected_fragmentation;
    int rejected_capacity;
    int terminated;
    int errors;
    int samples;
    double frag_sum;
    double frag_peak;
    StrList rejected_vms;   /* e.g. "F(290 MB)" */
    StrList error_messages; /* e.g. "line 7: unknown command 'FOO'" */
} RunMetrics;

double metrics_average_fragmentation(const RunMetrics *m);
void metrics_free(RunMetrics *m);

typedef struct {
    char strategy[16];
    MemSize heap_size;
    RunMetrics metrics;
    HeapStats final_stats;
    char final_map[HT_MAP_WIDTH + 3];
} StrategyRun;

void strategy_run_free(StrategyRun *r);

/* ----------------------------------------------------------------- report.c */

/* Block-by-block listing followed by the ASCII memory map. Appends to `out`. */
int report_snapshot_lines(const Heap *h, const VMManager *m, StrList *out);
/* "|AAAA....BB|": first letter of the owning VM's name, '.' for free memory. */
void report_memory_map(const Heap *h, const VMManager *m, int width, char *buf, size_t buf_len);
void report_stats_line(const HeapStats *s, char *buf, size_t buf_len);
int report_comparison_lines(const char *title, const StrategyRun *runs, size_t n, StrList *out);

/* -------------------------------------------------------------- processor.c */

typedef enum {
    CMD_OK,
    CMD_REJECTED, /* CREATE refused by the allocator (heap unchanged) */
    CMD_SKIPPED,  /* TERMINATE of a VM whose creation was rejected earlier */
    CMD_ERROR,    /* syntax / usage error, line ignored */
    CMD_IGNORED,  /* blank line, comment, or command not applicable in this mode */
    CMD_QUIT
} CommandStatus;

typedef struct {
    int print_ops;     /* print one line per command */
    int print_stats;   /* print statistics after CREATE / TERMINATE */
    int lock_strategy; /* ignore STRATEGY and COMPARE (used while replaying for comparison) */
} ProcessorOptions;

typedef struct Processor {
    FILE *out; /* may be NULL (silent) */
    SessionLog *log; /* may be NULL */
    ProcessorOptions opt;
    Heap *heap;
    VMManager *vms;
    RunMetrics metrics;
    long seq;
    StrList history; /* HEAP / CREATE / TERMINATE lines that were accepted */
} Processor;

ProcessorOptions processor_default_options(void);
/* NULL for a bad heap size, unknown strategy, or out of memory. */
Processor *processor_create(MemSize heap_size, const char *strategy_name, FILE *out, SessionLog *log,
                            ProcessorOptions opt);
void processor_destroy(Processor *p);
CommandStatus processor_execute(Processor *p, const char *raw_line, int line_no);
void processor_finish(Processor *p); /* final snapshot, statistics and summary */
void processor_help_lines(StrList *out);

/* --------------------------------------------------------------- compare.c */

/* Replays `lines` on a fresh heap once per strategy (first-, best-, worst-fit).
 * Fills runs[0..2]; returns 1 on success, 0 on out-of-memory. Free each run with
 * strategy_run_free(). */
int compare_run(const StrList *lines, MemSize default_heap, SessionLog *log, StrategyRun runs[3]);

/* --------------------------------------------------------------- tracegen.c */

/* Appends a deterministic pseudo-random trace (identical to the C++ generator). */
int generate_trace(MemSize heap_size, int ops, unsigned long long seed, StrList *out);

#endif
