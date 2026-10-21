#include "heaptrace.h"

#include <stdlib.h>
#include <string.h>

static long g_live_blocks = 0;

long ht_live_blocks(void) { return g_live_blocks; }

static MemoryBlock *block_new(MemSize start, MemSize size, int is_free, int owner_id) {
    MemoryBlock *b = (MemoryBlock *)malloc(sizeof(MemoryBlock));
    if (b == NULL) return NULL;
    b->start = start;
    b->size = size;
    b->is_free = is_free;
    b->owner_id = owner_id;
    b->next = NULL;
    b->prev = NULL;
    ++g_live_blocks;
    return b;
}

static void block_delete(MemoryBlock *b) {
    free(b);
    --g_live_blocks;
}

/* ---- strategies (the C counterpart of AllocationStrategy subclasses) ------ */

static MemoryBlock *first_fit_select(MemoryBlock *head, MemSize size) {
    MemoryBlock *b;
    for (b = head; b != NULL; b = b->next) {
        if (b->is_free && b->size >= size) return b;
    }
    return NULL;
}

static MemoryBlock *best_fit_select(MemoryBlock *head, MemSize size) {
    MemoryBlock *b, *best = NULL;
    for (b = head; b != NULL; b = b->next) {
        if (b->is_free && b->size >= size && (best == NULL || b->size < best->size)) best = b;
    }
    return best;
}

static MemoryBlock *worst_fit_select(MemoryBlock *head, MemSize size) {
    MemoryBlock *b, *worst = NULL;
    for (b = head; b != NULL; b = b->next) {
        if (b->is_free && b->size >= size && (worst == NULL || b->size > worst->size)) worst = b;
    }
    return worst;
}

const Strategy HT_FIRST_FIT = {"first-fit", first_fit_select};
const Strategy HT_BEST_FIT = {"best-fit", best_fit_select};
const Strategy HT_WORST_FIT = {"worst-fit", worst_fit_select};

const Strategy *ht_strategy_by_name(const char *name) {
    char buf[32];
    if (name == NULL || strlen(name) >= sizeof(buf)) return NULL;
    strcpy(buf, name);
    ht_to_lower(buf);
    if (strcmp(buf, "first") == 0 || strcmp(buf, "first-fit") == 0 || strcmp(buf, "firstfit") == 0) return &HT_FIRST_FIT;
    if (strcmp(buf, "best") == 0 || strcmp(buf, "best-fit") == 0 || strcmp(buf, "bestfit") == 0) return &HT_BEST_FIT;
    if (strcmp(buf, "worst") == 0 || strcmp(buf, "worst-fit") == 0 || strcmp(buf, "worstfit") == 0) return &HT_WORST_FIT;
    return NULL;
}

/* ---- heap ---------------------------------------------------------------- */

Heap *heap_create(MemSize total) {
    Heap *h;
    if (total < 1 || total > HT_MAX_MEMORY_MB) return NULL;
    h = (Heap *)malloc(sizeof(Heap));
    if (h == NULL) return NULL;
    h->head = block_new(0, total, 1, HT_NO_OWNER);
    if (h->head == NULL) {
        free(h);
        return NULL;
    }
    h->total = total;
    h->strategy = &HT_FIRST_FIT;
    return h;
}

void heap_destroy(Heap *h) {
    MemoryBlock *b, *nxt;
    if (h == NULL) return;
    for (b = h->head; b != NULL; b = nxt) {
        nxt = b->next;
        block_delete(b);
    }
    free(h);
}

void heap_set_strategy(Heap *h, const Strategy *s) {
    if (s != NULL) h->strategy = s;
}

static int owns_block(const Heap *h, int owner_id) {
    const MemoryBlock *b;
    for (b = h->head; b != NULL; b = b->next) {
        if (!b->is_free && b->owner_id == owner_id) return 1;
    }
    return 0;
}

AllocationResult heap_allocate(Heap *h, int owner_id, MemSize size) {
    AllocationResult result;
    MemoryBlock *chosen;
    result.success = 0;
    result.start = -1;
    result.reason = HT_REJECT_INVALID;
    if (owner_id < 0 || size < 1 || size > HT_MAX_MEMORY_MB || owns_block(h, owner_id)) return result;

    chosen = h->strategy->select_block(h->head, size);
    /* Defensive: never trust a strategy blindly. */
    if (chosen == NULL || !chosen->is_free || chosen->size < size) {
        result.reason = (heap_stats(h).free_memory >= size) ? HT_REJECT_FRAGMENTATION : HT_REJECT_CAPACITY;
        return result;
    }

    if (chosen->size > size) {
        /* Split: the front part serves the request, the tail stays free. */
        MemoryBlock *rest = block_new(chosen->start + size, chosen->size - size, 1, HT_NO_OWNER);
        if (rest == NULL) return result; /* out of memory: heap unchanged, reported as invalid */
        rest->prev = chosen;
        rest->next = chosen->next;
        if (chosen->next != NULL) chosen->next->prev = rest;
        chosen->next = rest;
        chosen->size = size;
    }
    chosen->is_free = 0;
    chosen->owner_id = owner_id;

    result.success = 1;
    result.start = chosen->start;
    result.reason = HT_REJECT_NONE;
    return result;
}

ReleaseResult heap_release(Heap *h, int owner_id) {
    ReleaseResult result;
    MemoryBlock *b;
    result.found = 0;
    result.start = -1;
    result.size = 0;
    result.merges = 0;
    if (owner_id < 0) return result;

    b = h->head;
    while (b != NULL && (b->is_free || b->owner_id != owner_id)) b = b->next;
    if (b == NULL) return result;

    result.found = 1;
    result.start = b->start;
    result.size = b->size;
    b->is_free = 1;
    b->owner_id = HT_NO_OWNER;

    /* Merge with the following block if it is free. */
    if (b->next != NULL && b->next->is_free) {
        MemoryBlock *n = b->next;
        b->size += n->size;
        b->next = n->next;
        if (n->next != NULL) n->next->prev = b;
        block_delete(n);
        ++result.merges;
    }
    /* Merge into the preceding block if it is free. */
    if (b->prev != NULL && b->prev->is_free) {
        MemoryBlock *p = b->prev;
        p->size += b->size;
        p->next = b->next;
        if (b->next != NULL) b->next->prev = p;
        block_delete(b);
        ++result.merges;
    }
    return result;
}

HeapStats heap_stats(const Heap *h) {
    HeapStats s;
    const MemoryBlock *b;
    memset(&s, 0, sizeof(s));
    s.total = h->total;
    for (b = h->head; b != NULL; b = b->next) {
        if (b->is_free) {
            ++s.free_blocks;
            s.free_memory += b->size;
            if (b->size > s.largest_free) s.largest_free = b->size;
        } else {
            ++s.used_blocks;
            s.used += b->size;
        }
    }
    s.external_fragmentation =
        (s.free_memory > 0) ? 1.0 - (double)s.largest_free / (double)s.free_memory : 0.0;
    return s;
}

const MemoryBlock *heap_block_at(const Heap *h, MemSize pos) {
    const MemoryBlock *b;
    if (pos < 0 || pos >= h->total) return NULL;
    for (b = h->head; b != NULL; b = b->next) {
        if (pos >= b->start && pos < b->start + b->size) return b;
    }
    return NULL;
}

int heap_validate(const Heap *h, char *err, size_t err_len) {
    char msg[160];
    MemSize expected = 0;
    const MemoryBlock *prev = NULL;
    const MemoryBlock *b;
    msg[0] = '\0';
    for (b = h->head; b != NULL && msg[0] == '\0'; b = b->next) {
        if (b->prev != prev) {
            snprintf(msg, sizeof(msg), "broken prev link at address %lld", b->start);
        } else if (b->start != expected) {
            snprintf(msg, sizeof(msg), "gap or overlap: block starts at %lld, expected %lld", b->start, expected);
        } else if (b->size < 1) {
            snprintf(msg, sizeof(msg), "empty block at address %lld", b->start);
        } else if (b->is_free && b->owner_id != HT_NO_OWNER) {
            snprintf(msg, sizeof(msg), "free block at %lld has an owner", b->start);
        } else if (!b->is_free && b->owner_id < 0) {
            snprintf(msg, sizeof(msg), "used block at %lld has no owner", b->start);
        } else if (b->is_free && prev != NULL && prev->is_free) {
            snprintf(msg, sizeof(msg), "adjacent free blocks at %lld were not merged", b->start);
        }
        expected = b->start + b->size;
        prev = b;
    }
    if (msg[0] == '\0' && expected != h->total) {
        snprintf(msg, sizeof(msg), "blocks cover %lld MB but the heap is %lld MB", expected, h->total);
    }
    if (msg[0] == '\0' && h->head == NULL) snprintf(msg, sizeof(msg), "heap list is empty");
    if (msg[0] != '\0' && err != NULL && err_len > 0) {
        strncpy(err, msg, err_len - 1);
        err[err_len - 1] = '\0';
    }
    return msg[0] == '\0';
}
