/* alloc_track.c - counting wrappers around the real allocator (test builds only).
 * Compile this file WITHOUT -include alloc_track.h. */
#include <stdlib.h>

static long g_outstanding = 0;

void *ht_track_malloc(size_t n) {
    void *p = malloc(n);
    if (p != NULL) ++g_outstanding;
    return p;
}

void *ht_track_calloc(size_t count, size_t n) {
    void *p = calloc(count, n);
    if (p != NULL) ++g_outstanding;
    return p;
}

void *ht_track_realloc(void *old, size_t n) {
    void *p = realloc(old, n);
    if (old == NULL && p != NULL) ++g_outstanding;
    return p;
}

void ht_track_free(void *p) {
    if (p != NULL) --g_outstanding;
    free(p);
}

long ht_track_outstanding(void) { return g_outstanding; }
