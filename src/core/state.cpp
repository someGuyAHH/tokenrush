#include "state.hpp"

#include <iomanip>
#include <sstream>

void MessageLog::push(Sev sev, const std::string& text) {
    std::lock_guard lk(m_);
    std::time_t t = std::time(nullptr);
    std::ostringstream os;
    os << std::put_time(std::localtime(&t), "%H:%M:%S");   // localtime is only ever called under m_

    LogEntry e{os.str(), sev, text};
    pending_.push_back(e);
    history_.push_back(std::move(e));
    if (history_.size() > HISTORY_MAX) history_.pop_front();
    if (pending_.size() > HISTORY_MAX) pending_.pop_front();   // nobody is draining (e.g. a headless run)
}

std::vector<LogEntry> MessageLog::drain() {
    std::lock_guard lk(m_);
    std::vector<LogEntry> out(pending_.begin(), pending_.end());
    pending_.clear();
    return out;
}

bool MessageLog::hasPending() {
    std::lock_guard lk(m_);
    return !pending_.empty();
}

std::vector<LogEntry> MessageLog::recent(size_t n) {
    std::lock_guard lk(m_);
    size_t start = history_.size() > n ? history_.size() - n : 0;
    return std::vector<LogEntry>(history_.begin() + (std::ptrdiff_t)start, history_.end());
}
