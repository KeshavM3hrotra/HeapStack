// Util.h - small string/number helpers shared by the parser, reporter and CLI.
#ifndef HEAPTRACE_UTIL_H
#define HEAPTRACE_UTIL_H

#include "Types.h"

#include <string>
#include <vector>

namespace heaptrace {
namespace util {

// printf-style formatting into a std::string.
#if defined(__GNUC__)
std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#else
std::string format(const char* fmt, ...);
#endif

std::string toLower(const std::string& s);
std::string toUpper(const std::string& s);
std::string trim(const std::string& s);
std::vector<std::string> splitWhitespace(const std::string& s);

// Parses a plain decimal integer (digits only, no sign, no spaces) in [lo, hi].
bool parseInteger(const std::string& token, long long lo, long long hi, long long& out);

// Parses a memory size in MB: a whole number in [1, MAX_MEMORY_MB].
bool parseSize(const std::string& token, MemSize& out);

// VM names: 1-32 characters from [A-Za-z0-9_.-], first character alphanumeric or '_'.
bool isValidVmName(const std::string& name);

}  // namespace util
}  // namespace heaptrace

#endif
