/*
 * alloc_track.h - test-only leak detector for the C implementation.
 *
 * The test build force-includes this header (-include) into every source file, so
 * each malloc/calloc/realloc/free in HeapTrace is counted. The test program fails
 * if any allocation is still outstanding at exit. Not used in normal builds.
 */
#ifndef HEAPTRACE_ALLOC_TRACK_H
#define HEAPTRACE_ALLOC_TRACK_H

#include <stdlib.h>

void *ht_track_malloc(size_t n);
void *ht_track_calloc(size_t count, size_t n);
void *ht_track_realloc(void *p, size_t n);
void ht_track_free(void *p);
long ht_track_outstanding(void);

#define malloc ht_track_malloc
#define calloc ht_track_calloc
#define realloc ht_track_realloc
#define free ht_track_free

#endif
