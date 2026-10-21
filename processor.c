/*
 * processor.c - command interpreter shared by trace files and the console.
 *
 *   HEAP <size_mb> | CREATE <name> <size_mb> | TERMINATE <name>
 *   STRATEGY <first|best|worst> | SNAPSHOT | STATS | COMPARE | HELP | QUIT
 *
 * '#' starts a comment. Commands are case-insensitive; VM names are not.
 */
#include "heaptrace.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 16

/* ---- output helpers ------------------------------------------------------ */

static void say(const Processor *p, const char *text) {
    if (p->out != NULL) {
        fputs(text, p->out);
        fputc('\n', p->out);
    }
}

static void log_line(const Processor *p, const char *text) {
    if (p->log != NULL) log_write(p->log, text);
}

static char *vformat_msg(const char *fmt, va_list ap) {
    va_list ap2;
    int n;
    char *buf;
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    buf = (n < 0) ? NULL : (char *)malloc((size_t)n + 1);
    if (buf != NULL) vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return buf != NULL ? buf : ht_strdup("(out of memory)");
}

static void sample(Processor *p) {
    HeapStats s = heap_stats(p->heap);
    ++p->metrics.samples;
    p->metrics.frag_sum += s.external_fragmentation;
    if (s.external_fragmentation > p->metrics.frag_peak) p->metrics.frag_peak = s.external_fragmentation;
}

/* Prints and logs the outcome of one command. */
static void report(Processor *p, const char *line, const char *status, int with_stats, const char *fmt, ...)
    HT_PRINTF(5, 6);

static void report(Processor *p, const char *line, const char *status, int with_stats, const char *fmt, ...) {
    va_list ap;
    char *message, *text;
    char stats[512];
    HeapStats s = heap_stats(p->heap);

    va_start(ap, fmt);
    message = vformat_msg(fmt, ap);
    va_end(ap);
    report_stats_line(&s, stats, sizeof(stats));

    if (p->opt.print_ops) {
        text = ht_asprintf("%-11s%s", status, message);
        if (text != NULL) say(p, text);
        free(text);
        if (with_stats && p->opt.print_stats) {
            text = ht_asprintf("           -> %s", stats);
            if (text != NULL) say(p, text);
            free(text);
        }
    }
    if (with_stats) {
        text = ht_asprintf("#%ld %s => %s %s | %s", p->seq, line, status, message, stats);
    } else {
        text = ht_asprintf("#%ld %s => %s %s", p->seq, line, status, message);
    }
    if (text != NULL) log_line(p, text);
    free(text);
    free(message);
}

static CommandStatus fail(Processor *p, int line_no, const char *line, const char *fmt, ...) HT_PRINTF(4, 5);

static CommandStatus fail(Processor *p, int line_no, const char *line, const char *fmt, ...) {
    va_list ap;
    char *message, *text, *shown, *entry;

    va_start(ap, fmt);
    message = vformat_msg(fmt, ap);
    va_end(ap);

    ++p->metrics.errors;
    text = ht_asprintf("line %d: %s", line_no, message);
    if (text != NULL) {
        strlist_push(&p->metrics.error_messages, text);
        if (p->opt.print_ops) {
            shown = ht_asprintf("%-11s%s", "[ERROR]", text);
            if (shown != NULL) say(p, shown);
            free(shown);
        }
        entry = ht_asprintf("#%ld %s => [ERROR] %s", p->seq, line, text);
        if (entry != NULL) log_line(p, entry);
        free(entry);
    }
    free(text);
    free(message);
    return CMD_ERROR;
}

/* ---- lifecycle ----------------------------------------------------------- */

ProcessorOptions processor_default_options(void) {
    ProcessorOptions o;
    o.print_ops = 1;
    o.print_stats = 1;
    o.lock_strategy = 0;
    return o;
}

Processor *processor_create(MemSize heap_size, const char *strategy_name, FILE *out, SessionLog *log,
                            ProcessorOptions opt) {
    const Strategy *strategy = ht_strategy_by_name(strategy_name);
    Processor *p;
    if (strategy == NULL) return NULL;
    p = (Processor *)calloc(1, sizeof(Processor));
    if (p == NULL) return NULL;
    p->out = out;
    p->log = log;
    p->opt = opt;
    p->heap = heap_create(heap_size);
    p->vms = (p->heap != NULL) ? vm_manager_create(p->heap) : NULL;
    if (p->heap == NULL || p->vms == NULL || !strlist_pushf(&p->history, "HEAP %lld", heap_size)) {
        processor_destroy(p);
        return NULL;
    }
    heap_set_strategy(p->heap, strategy);
    return p;
}

void processor_destroy(Processor *p) {
    if (p == NULL) return;
    vm_manager_destroy(p->vms);
    heap_destroy(p->heap);
    metrics_free(&p->metrics);
    strlist_free(&p->history);
    free(p);
}

void processor_help_lines(StrList *out) {
    strlist_push(out, "Commands (case-insensitive, '#' starts a comment):");
    strlist_push(out, "  HEAP <size_mb>             re-initialise the server memory (discards all VMs)");
    strlist_push(out, "  CREATE <name> <size_mb>    create a VM = allocate a block");
    strlist_push(out, "  TERMINATE <name>           terminate a VM = free its block");
    strlist_push(out, "  STRATEGY <first|best|worst> switch the placement strategy");
    strlist_push(out, "  SNAPSHOT                   show the heap block by block");
    strlist_push(out, "  STATS                      show fragmentation statistics");
    strlist_push(out, "  COMPARE                    replay this session under all strategies");
    strlist_push(out, "  HELP                       show this help");
    strlist_push(out, "  QUIT                       leave");
}

/* ---- command handlers ---------------------------------------------------- */

static CommandStatus do_heap(Processor *p, char **tok, int ntok, const char *line, int line_no) {
    MemSize size = 0;
    Heap *fresh;
    VMManager *fresh_vms;
    if (ntok != 2) return fail(p, line_no, line, "usage: HEAP <size_mb>");
    if (!ht_parse_size(tok[1], &size)) {
        return fail(p, line_no, line,
                    "invalid heap size '%s' (expected a whole number of MB from 1 to 1000000000000)", tok[1]);
    }
    fresh = heap_create(size);
    fresh_vms = (fresh != NULL) ? vm_manager_create(fresh) : NULL;
    if (fresh == NULL || fresh_vms == NULL) {
        vm_manager_destroy(fresh_vms);
        heap_destroy(fresh);
        return fail(p, line_no, line, "out of memory");
    }
    heap_set_strategy(fresh, p->heap->strategy);
    vm_manager_destroy(p->vms); /* the manager refers to the old heap */
    heap_destroy(p->heap);
    p->heap = fresh;
    p->vms = fresh_vms;
    strlist_clear(&p->history);
    strlist_pushf(&p->history, "HEAP %lld", size);
    report(p, line, "[OK]", 0, "HEAP %lld MB -> heap initialised, all VMs discarded", size);
    return CMD_OK;
}

static CommandStatus do_create(Processor *p, char **tok, int ntok, const char *line, int line_no) {
    MemSize size = 0;
    const char *name;
    CreateResult r;
    HeapStats s;

    if (ntok != 3) return fail(p, line_no, line, "usage: CREATE <name> <size_mb>");
    name = tok[1];
    if (!ht_valid_vm_name(name)) {
        return fail(p, line_no, line,
                    "invalid VM name '%s' (1-32 characters: letters, digits, _ - . ; must start with a letter, digit or _)",
                    name);
    }
    if (!ht_parse_size(tok[2], &size)) {
        return fail(p, line_no, line, "invalid size '%s' (expected a whole number of MB from 1 to 1000000000000)",
                    tok[2]);
    }

    r = vm_create(p->vms, name, size);
    if (r.failed) return fail(p, line_no, line, "out of memory");
    if (r.duplicate) return fail(p, line_no, line, "a VM named '%s' is already running", name);

    strlist_pushf(&p->history, "CREATE %s %lld", name, size);
    if (r.alloc.success) {
        ++p->metrics.placed;
        sample(p);
        report(p, line, "[OK]", 1, "CREATE %s %lld MB -> placed at [%lld, %lld)", name, size, r.alloc.start,
               r.alloc.start + size);
        return CMD_OK;
    }

    ++p->metrics.rejected;
    strlist_pushf(&p->metrics.rejected_vms, "%s(%lld MB)", name, size);
    s = heap_stats(p->heap); /* a rejection leaves the heap unchanged */
    if (r.alloc.reason == HT_REJECT_FRAGMENTATION) {
        ++p->metrics.rejected_fragmentation;
        sample(p);
        report(p, line, "[REJECTED]", 1,
               "CREATE %s %lld MB -> no single free block is large enough (largest free %lld MB, total free %lld MB: external fragmentation)",
               name, size, s.largest_free, s.free_memory);
    } else {
        ++p->metrics.rejected_capacity;
        sample(p);
        report(p, line, "[REJECTED]", 1, "CREATE %s %lld MB -> only %lld MB free in total (insufficient memory)", name,
               size, s.free_memory);
    }
    return CMD_REJECTED;
}

static CommandStatus do_terminate(Processor *p, char **tok, int ntok, const char *line, int line_no) {
    const char *name;
    TerminateResult r;
    char merged[96];

    if (ntok != 2) return fail(p, line_no, line, "usage: TERMINATE <name>");
    name = tok[1];
    if (!ht_valid_vm_name(name)) return fail(p, line_no, line, "invalid VM name '%s'", name);

    r = vm_terminate(p->vms, name);
    if (r.status == TERM_NOT_FOUND) return fail(p, line_no, line, "no running VM named '%s'", name);

    strlist_pushf(&p->history, "TERMINATE %s", name);
    if (r.status == TERM_WAS_REJECTED) {
        report(p, line, "[SKIPPED]", 0, "TERMINATE %s -> VM was rejected earlier, nothing to release", name);
        return CMD_SKIPPED;
    }

    ++p->metrics.terminated;
    sample(p);
    merged[0] = '\0';
    if (r.release.merges > 0) {
        snprintf(merged, sizeof(merged), ", merged with %d adjacent free block%s", r.release.merges,
                 r.release.merges == 1 ? "" : "s");
    }
    report(p, line, "[OK]", 1, "TERMINATE %s -> released %lld MB at [%lld, %lld)%s", name, r.release.size,
           r.release.start, r.release.start + r.release.size, merged);
    return CMD_OK;
}

static CommandStatus do_strategy(Processor *p, char **tok, int ntok, const char *line, int line_no) {
    const Strategy *s;
    if (p->opt.lock_strategy) return CMD_IGNORED;
    if (ntok != 2) return fail(p, line_no, line, "usage: STRATEGY <first|best|worst>");
    s = ht_strategy_by_name(tok[1]);
    if (s == NULL) return fail(p, line_no, line, "unknown strategy '%s' (use first, best or worst)", tok[1]);
    heap_set_strategy(p->heap, s);
    report(p, line, "[OK]", 0, "STRATEGY -> %s", s->name);
    return CMD_OK;
}

static CommandStatus do_compare(Processor *p, int ntok, const char *line, int line_no) {
    StrategyRun runs[3];
    StrList text;
    char *title;
    size_t i;

    if (p->opt.lock_strategy) return CMD_IGNORED;
    if (ntok != 1) return fail(p, line_no, line, "usage: COMPARE takes no arguments");
    if (!compare_run(&p->history, p->heap->total, p->log, runs)) return fail(p, line_no, line, "out of memory");

    memset(&text, 0, sizeof(text));
    title = ht_asprintf("current session (%d commands)", (int)p->history.count - 1);
    if (title == NULL || !report_comparison_lines(title, runs, 3, &text)) {
        free(title);
        strlist_free(&text);
        for (i = 0; i < 3; ++i) strategy_run_free(&runs[i]);
        return fail(p, line_no, line, "out of memory");
    }
    free(title);
    {
        char *head = ht_asprintf("#%ld %s", p->seq, line);
        if (head != NULL) log_line(p, head);
        free(head);
    }
    for (i = 0; i < text.count; ++i) {
        char *indented = ht_asprintf("    %s", text.items[i]);
        if (p->opt.print_ops) say(p, text.items[i]);
        if (indented != NULL) log_line(p, indented);
        free(indented);
    }
    strlist_free(&text);
    for (i = 0; i < 3; ++i) strategy_run_free(&runs[i]);
    return CMD_OK;
}

/* Splits `buf` (modified in place) into at most MAX_TOKENS tokens; returns the real token count. */
static int split_tokens(char *buf, char **tok) {
    int count = 0;
    char *s = buf;
    while (*s != '\0') {
        char *start;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == '\v' || *s == '\f') ++s;
        if (*s == '\0') break;
        start = s;
        while (*s != '\0' && !(*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == '\v' || *s == '\f')) ++s;
        if (count < MAX_TOKENS) tok[count] = start;
        ++count;
        if (*s != '\0') *s++ = '\0';
    }
    return count;
}

CommandStatus processor_execute(Processor *p, const char *raw_line, int line_no) {
    char *work, *line, *copy, *hash;
    char *tok[MAX_TOKENS];
    char cmd[16];
    int ntok;
    CommandStatus status;

    work = ht_strdup(raw_line);
    if (work == NULL) return CMD_ERROR;
    hash = strchr(work, '#');
    if (hash != NULL) *hash = '\0';
    line = ht_trim(work);
    if (*line == '\0') {
        free(work);
        return CMD_IGNORED;
    }

    copy = ht_strdup(line);
    if (copy == NULL) {
        free(work);
        return CMD_ERROR;
    }
    memset(tok, 0, sizeof(tok));
    ntok = split_tokens(copy, tok);
    strncpy(cmd, tok[0], sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    ht_to_upper(cmd);
    ++p->seq;

    if (strcmp(cmd, "HEAP") == 0) {
        status = do_heap(p, tok, ntok, line, line_no);
    } else if (strcmp(cmd, "CREATE") == 0) {
        status = do_create(p, tok, ntok, line, line_no);
    } else if (strcmp(cmd, "TERMINATE") == 0) {
        status = do_terminate(p, tok, ntok, line, line_no);
    } else if (strcmp(cmd, "STRATEGY") == 0) {
        status = do_strategy(p, tok, ntok, line, line_no);
    } else if (strcmp(cmd, "COMPARE") == 0) {
        status = do_compare(p, ntok, line, line_no);
    } else if (strcmp(cmd, "SNAPSHOT") == 0 || strcmp(cmd, "STATS") == 0 || strcmp(cmd, "HELP") == 0 ||
               strcmp(cmd, "QUIT") == 0 || strcmp(cmd, "EXIT") == 0) {
        if (ntok != 1) {
            status = fail(p, line_no, line, "usage: %s takes no arguments", cmd);
        } else if (strcmp(cmd, "QUIT") == 0 || strcmp(cmd, "EXIT") == 0) {
            char *entry = ht_asprintf("#%ld %s => quit", p->seq, line);
            if (entry != NULL) log_line(p, entry);
            free(entry);
            status = CMD_QUIT;
        } else if (strcmp(cmd, "HELP") == 0) {
            if (p->opt.print_ops) {
                StrList h;
                size_t i;
                memset(&h, 0, sizeof(h));
                processor_help_lines(&h);
                for (i = 0; i < h.count; ++i) say(p, h.items[i]);
                strlist_free(&h);
            }
            status = CMD_OK;
        } else if (strcmp(cmd, "SNAPSHOT") == 0) {
            StrList snap;
            size_t i;
            char *head = ht_asprintf("#%ld %s", p->seq, line);
            memset(&snap, 0, sizeof(snap));
            report_snapshot_lines(p->heap, p->vms, &snap);
            if (head != NULL) log_line(p, head);
            free(head);
            for (i = 0; i < snap.count; ++i) {
                char *indented = ht_asprintf("    %s", snap.items[i]);
                if (p->opt.print_ops) say(p, snap.items[i]);
                if (indented != NULL) log_line(p, indented);
                free(indented);
            }
            strlist_free(&snap);
            status = CMD_OK;
        } else { /* STATS */
            char stats[512];
            char *entry;
            HeapStats s = heap_stats(p->heap);
            report_stats_line(&s, stats, sizeof(stats));
            if (p->opt.print_ops) {
                char *shown = ht_asprintf("Stats: %s", stats);
                if (shown != NULL) say(p, shown);
                free(shown);
            }
            entry = ht_asprintf("#%ld %s => %s", p->seq, line, stats);
            if (entry != NULL) log_line(p, entry);
            free(entry);
            status = CMD_OK;
        }
    } else {
        status = fail(p, line_no, line, "unknown command '%s' (type HELP for the list)", tok[0]);
    }

    free(copy);
    free(work);
    return status;
}

void processor_finish(Processor *p) {
    StrList snap;
    char stats[512];
    char *summary, *frag, *shown;
    HeapStats s = heap_stats(p->heap);
    size_t i;

    memset(&snap, 0, sizeof(snap));
    report_snapshot_lines(p->heap, p->vms, &snap);
    report_stats_line(&s, stats, sizeof(stats));
    summary = ht_asprintf("Summary: placed=%d, rejected=%d (fragmentation=%d, capacity=%d), terminated=%d, errors=%d",
                          p->metrics.placed, p->metrics.rejected, p->metrics.rejected_fragmentation,
                          p->metrics.rejected_capacity, p->metrics.terminated, p->metrics.errors);
    frag = ht_asprintf("External fragmentation over %d samples: average %.1f%%, peak %.1f%%", p->metrics.samples,
                       metrics_average_fragmentation(&p->metrics) * 100.0, p->metrics.frag_peak * 100.0);

    say(p, "");
    say(p, "=== Final heap state ===");
    log_line(p, "=== Final heap state ===");
    for (i = 0; i < snap.count; ++i) {
        say(p, snap.items[i]);
        log_line(p, snap.items[i]);
    }
    shown = ht_asprintf("Stats: %s", stats);
    if (shown != NULL) {
        say(p, shown);
        log_line(p, shown);
    }
    if (summary != NULL) {
        say(p, summary);
        log_line(p, summary);
    }
    if (frag != NULL) {
        say(p, frag);
        log_line(p, frag);
    }
    free(shown);
    free(summary);
    free(frag);
    strlist_free(&snap);
}
