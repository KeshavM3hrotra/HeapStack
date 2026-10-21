#include "heaptrace.h"

#include <stdlib.h>
#include <string.h>

void report_memory_map(const Heap *h, const VMManager *m, int width, char *buf, size_t buf_len) {
    int i;
    size_t pos_out = 0;
    if (buf_len == 0) return;
    if (pos_out + 1 < buf_len) buf[pos_out++] = '|';
    for (i = 0; i < width; ++i) {
        /* Sample the middle of each slice. */
        MemSize pos = ((2 * (MemSize)i + 1) * h->total) / (2 * (MemSize)width);
        const MemoryBlock *b = heap_block_at(h, pos);
        char c = '?';
        if (b != NULL) {
            if (b->is_free) {
                c = '.';
            } else {
                const VirtualMachine *vm = vm_find_by_id(m, b->owner_id);
                c = (vm != NULL && vm->name[0] != '\0') ? vm->name[0] : '?';
            }
        }
        if (pos_out + 1 < buf_len) buf[pos_out++] = c;
    }
    if (pos_out + 1 < buf_len) buf[pos_out++] = '|';
    buf[pos_out] = '\0';
}

int report_snapshot_lines(const Heap *h, const VMManager *m, StrList *out) {
    const MemoryBlock *b;
    char map[HT_MAP_WIDTH + 3];
    if (!strlist_pushf(out, "Heap snapshot [%s], total %lld MB", h->strategy->name, h->total)) return 0;
    for (b = h->head; b != NULL; b = b->next) {
        int ok;
        if (b->is_free) {
            ok = strlist_pushf(out, "  [%6lld,%6lld) %7lld MB  %s", b->start, b->start + b->size, b->size, "FREE");
        } else {
            const VirtualMachine *vm = vm_find_by_id(m, b->owner_id);
            ok = strlist_pushf(out, "  [%6lld,%6lld) %7lld MB  %s  %s (vm#%d)", b->start, b->start + b->size, b->size,
                               "USED", vm != NULL ? vm->name : "?", b->owner_id);
        }
        if (!ok) return 0;
    }
    report_memory_map(h, m, HT_MAP_WIDTH, map, sizeof(map));
    return strlist_pushf(out, "  map %s", map);
}

void report_stats_line(const HeapStats *s, char *buf, size_t buf_len) {
    snprintf(buf, buf_len,
             "used=%lld MB, free=%lld MB, free blocks=%d, largest free=%lld MB, external fragmentation=%.1f%%",
             s->used, s->free_memory, s->free_blocks, s->largest_free, s->external_fragmentation * 100.0);
}

int report_comparison_lines(const char *title, const StrategyRun *runs, size_t n, StrList *out) {
    size_t i, k;
    int fewest, winner_count = 0;
    StrBuf winners;
    int ok = 1;

    if (!strlist_pushf(out, "=== Strategy comparison: %s ===", title)) return 0;
    if (n == 0) return 1;
    if (!strlist_pushf(out, "Heap size: %lld MB", runs[0].heap_size)) return 0;

    if (runs[0].metrics.errors > 0) {
        if (!strlist_pushf(out, "Trace lines skipped because of errors: %d", runs[0].metrics.errors)) return 0;
        for (k = 0; k < runs[0].metrics.error_messages.count; ++k) {
            if (!strlist_pushf(out, "  %s", runs[0].metrics.error_messages.items[k])) return 0;
        }
    }
    if (!strlist_push(out, "")) return 0;

    if (!strlist_pushf(out, "%-10s %6s %9s %10s %9s %12s %10s %10s %8s %9s", "Strategy", "Placed", "Rejected",
                       "(by frag)", "FreeBlks", "LargestFree", "TotalFree", "FinalFrag", "AvgFrag", "PeakFrag")) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        const StrategyRun *r = &runs[i];
        if (!strlist_pushf(out, "%-10s %6d %9d %10d %9d %12lld %10lld %9.1f%% %7.1f%% %8.1f%%", r->strategy,
                           r->metrics.placed, r->metrics.rejected, r->metrics.rejected_fragmentation,
                           r->final_stats.free_blocks, r->final_stats.largest_free, r->final_stats.free_memory,
                           r->final_stats.external_fragmentation * 100.0,
                           metrics_average_fragmentation(&r->metrics) * 100.0, r->metrics.frag_peak * 100.0)) {
            return 0;
        }
    }
    if (!strlist_push(out, "")) return 0;

    if (!strlist_push(out, "Rejected VMs:")) return 0;
    for (i = 0; i < n; ++i) {
        StrBuf list;
        memset(&list, 0, sizeof(list));
        for (k = 0; k < runs[i].metrics.rejected_vms.count; ++k) {
            if (k > 0) ok = ok && strbuf_append(&list, ", ");
            ok = ok && strbuf_append(&list, runs[i].metrics.rejected_vms.items[k]);
        }
        if (list.len == 0) ok = ok && strbuf_append(&list, "none");
        ok = ok && strlist_pushf(out, "  %-10s %s", runs[i].strategy, list.data != NULL ? list.data : "");
        strbuf_free(&list);
        if (!ok) return 0;
    }
    if (!strlist_push(out, "")) return 0;

    if (!strlist_push(out, "Final memory maps ('.' = free, letter = first letter of the VM name):")) return 0;
    for (i = 0; i < n; ++i) {
        if (!strlist_pushf(out, "  %-10s %s", runs[i].strategy, runs[i].final_map)) return 0;
    }
    if (!strlist_push(out, "")) return 0;

    fewest = runs[0].metrics.rejected;
    for (i = 1; i < n; ++i) {
        if (runs[i].metrics.rejected < fewest) fewest = runs[i].metrics.rejected;
    }
    memset(&winners, 0, sizeof(winners));
    for (i = 0; i < n; ++i) {
        if (runs[i].metrics.rejected == fewest) {
            if (winner_count > 0) ok = ok && strbuf_append(&winners, ", ");
            ok = ok && strbuf_append(&winners, runs[i].strategy);
            ++winner_count;
        }
    }
    if (ok) {
        if (winner_count == (int)n) {
            ok = strlist_pushf(out, "All strategies rejected the same number of VMs (%d).", fewest);
        } else {
            ok = strlist_pushf(out, "Fewest rejected VMs: %s  (result for this trace only, not a general ranking)",
                               winners.data != NULL ? winners.data : "");
        }
    }
    strbuf_free(&winners);
    return ok;
}
