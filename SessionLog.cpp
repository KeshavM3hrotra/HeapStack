#include "SessionLog.h"

namespace heaptrace {

bool SessionLog::open(const std::string& path) {
    close();
    file_.clear();
    file_.open(path.c_str(), std::ios::out | std::ios::trunc);
    if (!file_.is_open()) return false;
    path_ = path;
    return true;
}

void SessionLog::write(const std::string& line) {
    if (!file_.is_open()) return;
    file_ << line << '\n';
    file_.flush();
}

void SessionLog::close() {
    if (file_.is_open()) file_.close();
}

}  // namespace heaptrace
