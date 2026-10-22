// TraceGenerator.h - deterministic pseudo-random trace generator.
//
// The same (heap, ops, seed) always yields the same trace on every platform
// (own 64-bit LCG, no <random>), and the C version produces identical output.
#ifndef HEAPTRACE_TRACEGENERATOR_H
#define HEAPTRACE_TRACEGENERATOR_H

#include "Types.h"

#include <string>
#include <vector>

namespace heaptrace {

// Returns the trace as lines: a comment header, "HEAP <n>", then `ops`
// CREATE / TERMINATE commands. About half of the operations are CREATEs (sizes
// from 1 to heap/10 MB, at least 1); TERMINATEs pick a random VM the generator
// created earlier. VM names V1, V2, ... are unique. The generator does not know
// which requests a strategy will reject; terminating a rejected VM is reported
// as [SKIPPED], never as an error.
std::vector<std::string> generateTrace(MemSize heapSize, int ops, unsigned long long seed);

}  // namespace heaptrace

#endif
