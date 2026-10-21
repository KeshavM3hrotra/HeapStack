#include "heaptrace.h"

#include <stdlib.h>

/* Same 64-bit LCG as the C++ generator, so both produce identical traces. */
static unsigned long long g_state;

static unsigned long long rng_next(void) {
    g_state = g_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return g_state >> 33;
}

int generate_trace(MemSize heap_size, int ops, unsigned long long seed, StrList *out) {
    MemSize max_size = heap_size / 10;
    int *live = NULL; /* numbers of the VMs created and not yet terminated: V<n> */
    size_t live_count = 0;
    int next_name = 1;
    int i;

    if (max_size < 1) max_size = 1;
    if (!strlist_pushf(out, "# HeapTrace generated trace: heap=%lld MB, ops=%d, seed=%llu", heap_size, ops, seed) ||
        !strlist_pushf(out, "HEAP %lld", heap_size)) {
        return 0;
    }
    if (ops > 0) {
        live = (int *)malloc((size_t)ops * sizeof(int));
        if (live == NULL) return 0;
    }

    g_state = seed;
    for (i = 0; i < ops; ++i) {
        int create = (live_count == 0) || (rng_next() % 100) < 50;
        int ok;
        if (create) {
            MemSize size = 1 + (MemSize)(rng_next() % (unsigned long long)max_size);
            ok = strlist_pushf(out, "CREATE V%d %lld", next_name, size);
            live[live_count++] = next_name++;
        } else {
            size_t idx = (size_t)(rng_next() % live_count);
            ok = strlist_pushf(out, "TERMINATE V%d", live[idx]);
            live[idx] = live[live_count - 1];
            --live_count;
        }
        if (!ok) {
            free(live);
            return 0;
        }
    }
    free(live);
    return 1;
}
