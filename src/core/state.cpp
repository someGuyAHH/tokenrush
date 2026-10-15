#include "state.hpp"

#include <iomanip>
#include <sstream>

void MessageLog::push(Sev sev, const std::string& text) {
    std::lock_guard lk(m_);
    std::time_t t = std::time(nullptr);
    std::ostringstream os;
    os << std::put_time(std::localtime(&t), "%H:%M:%S");   // localtime is only ever called under m_

    history_.push_back(LogEntry{os.str(), sev, text});
    if (history_.size() > HISTORY_MAX) history_.pop_front();
}

std::vector<LogEntry> MessageLog::recent(size_t n) {
    std::lock_guard lk(m_);
    size_t start = history_.size() > n ? history_.size() - n : 0;
    return std::vector<LogEntry>(history_.begin() + (std::ptrdiff_t)start, history_.end());
}
