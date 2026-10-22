// SessionLog.h - fstream-based session log (persistence requirement).
#ifndef HEAPTRACE_SESSIONLOG_H
#define HEAPTRACE_SESSIONLOG_H

#include "Types.h"

#include <fstream>
#include <string>

namespace heaptrace {

class SessionLog {
public:
    SessionLog() {}

    // Opens (and truncates) the file. Returns false if it cannot be opened.
    bool open(const std::string& path);
    bool isOpen() const { return file_.is_open(); }
    const std::string& path() const { return path_; }

    // Appends one line and flushes, so the log survives a crash. No-op if closed.
    void write(const std::string& line);
    void close();

private:
    SessionLog(const SessionLog&);
    SessionLog& operator=(const SessionLog&);

    std::ofstream file_;
    std::string path_;
};

}  // namespace heaptrace

#endif
