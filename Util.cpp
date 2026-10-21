#include "Util.h"

#include <cstdarg>
#include <cstdio>

namespace heaptrace {
namespace util {

std::string format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    const int n = std::vsnprintf(0, 0, fmt, ap);
    va_end(ap);
    std::string result;
    if (n > 0) {
        std::vector<char> buf(static_cast<std::size_t>(n) + 1);
        std::vsnprintf(&buf[0], buf.size(), fmt, ap2);
        result.assign(&buf[0], static_cast<std::size_t>(n));
    }
    va_end(ap2);
    return result;
}

std::string toLower(const std::string& s) {
    std::string r(s);
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (r[i] >= 'A' && r[i] <= 'Z') r[i] = static_cast<char>(r[i] - 'A' + 'a');
    }
    return r;
}

std::string toUpper(const std::string& s) {
    std::string r(s);
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (r[i] >= 'a' && r[i] <= 'z') r[i] = static_cast<char>(r[i] - 'a' + 'A');
    }
    return r;
}

static bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> splitWhitespace(const std::string& s) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        const std::size_t start = i;
        while (i < s.size() && !isSpace(s[i])) ++i;
        if (i > start) tokens.push_back(s.substr(start, i - start));
    }
    return tokens;
}

bool parseInteger(const std::string& token, long long lo, long long hi, long long& out) {
    if (token.empty() || token.size() > 18) return false;  // 18 digits always fit in long long
    long long value = 0;
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (token[i] < '0' || token[i] > '9') return false;
        value = value * 10 + (token[i] - '0');
    }
    if (value < lo || value > hi) return false;
    out = value;
    return true;
}

bool parseSize(const std::string& token, MemSize& out) {
    long long v = 0;
    if (!parseInteger(token, 1, MAX_MEMORY_MB, v)) return false;
    out = v;
    return true;
}

bool isValidVmName(const std::string& name) {
    if (name.empty() || name.size() > 32) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (i == 0) {
            if (!alnum && c != '_') return false;
        } else if (!alnum && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

}  // namespace util
}  // namespace heaptrace
