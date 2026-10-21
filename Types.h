// Types.h - shared constants and types for the HeapTrace C++ simulator.
//
// This header must be the first thing included (directly or indirectly) so the
// MinGW printf switch below takes effect before any standard header.
#ifndef HEAPTRACE_TYPES_H
#define HEAPTRACE_TYPES_H

#if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1  // make %lld / %.1f behave identically on MinGW
#endif

namespace heaptrace {

// All sizes and addresses are whole megabytes.
typedef long long MemSize;

// Upper bound for any single size (10^12 MB). Keeps every sum and product in
// the program far below the 64-bit overflow limit.
const MemSize MAX_MEMORY_MB = 1000000000000LL;

// Owner id stored in a free block.
const int NO_OWNER = -1;

// Width (in characters) of the ASCII memory map.
const int MAP_WIDTH = 60;

}  // namespace heaptrace

#endif
