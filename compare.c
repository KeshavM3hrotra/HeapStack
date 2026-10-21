#include "heaptrace.h"

#include <stdlib.h>
#include <string.h>

int compare_run(const StrList *lines, MemSize default_heap, SessionLog *log, StrategyRun runs[3]) {
    static const char *const kStrategies[3] = {"first-fit", "best-fit", "worst-fit"};
    int s;
    for (s = 0; s < 3; ++s) {
        ProcessorOptions opt;
        Processor *p;
        size_t i;
        char *header;

        opt.print_ops = 0;
        opt.print_stats = 0;
        opt.lock_strategy = 1;
        p = processor_create(default_heap, kStrategies[s], NULL, log, opt); /* silent replay */
        if (p == NULL) {
            while (--s >= 0) strategy_run_free(&runs[s]);
            return 0;
        }
        header = ht_asprintf("--- comparison replay: %s ---", kStrategies[s]);
        if (header != NULL) {
            log_write(log, header);
            free(header);
        }

        for (i = 0; i < lines->count; ++i) {
            if (processor_execute(p, lines->items[i], (int)i + 1) == CMD_QUIT) break;
        }

        memset(&runs[s], 0, sizeof(runs[s]));
        strcpy(runs[s].strategy, kStrategies[s]);
        runs[s].heap_size = p->heap->total;
        runs[s].final_stats = heap_stats(p->heap);
        report_memory_map(p->heap, p->vms, HT_MAP_WIDTH, runs[s].final_map, sizeof(runs[s].final_map));
        runs[s].metrics = p->metrics;               /* take over the string lists ... */
        memset(&p->metrics, 0, sizeof(p->metrics)); /* ... so destroying p does not free them */
        processor_destroy(p);
    }
    return 1;
}
