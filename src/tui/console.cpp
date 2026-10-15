#include "console.hpp"

#include <istream>
#include <ostream>
#include <random>

#include "core/util.hpp"
#include "shell/commands.hpp"
#include "shell/term.hpp"

namespace tui {

namespace {
constexpr size_t KEEP_LINES = 1000;   // older console output is dropped
}

Console::Console(Shared& s, bool profiles, std::function<void(Outcome)> onExit)
    : s_(s), profiles_(profiles), onExit_(std::move(onExit)), thread_([this](std::stop_token st) { run(st); }) {}

Console::~Console() { stop(); }

void Console::stop() {
    {
        std::lock_guard lk(inM_);
        closed_ = true;
    }
    inCv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void Console::submit(const std::string& line) {
    append((line + "\n").c_str(), line.size() + 1);   // echo it after the prompt, like a terminal would
    std::lock_guard lk(inM_);
    pending_.push_back(line);
    if (!trim(line).empty()) history_.push_back(line);
    inCv_.notify_one();
}

std::vector<std::string> Console::lines(size_t last) const {
    std::lock_guard lk(outM_);
    size_t from = out_.size() > last ? out_.size() - last : 0;
    return std::vector<std::string>(out_.begin() + (std::ptrdiff_t)from, out_.end());
}

std::string Console::partial() const {
    std::lock_guard lk(outM_);
    return partial_;
}

std::vector<std::string> Console::history() const {
    std::lock_guard lk(inM_);
    return history_;
}

void Console::append(const char* s, size_t n) {
    std::lock_guard lk(outM_);
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == '\n') {
            out_.push_back(std::move(partial_));
            partial_.clear();
            if (out_.size() > KEEP_LINES) out_.pop_front();
        } else if (s[i] != '\r') {
            partial_ += s[i];
        }
    }
}

Console::InBuf::int_type Console::InBuf::underflow() {
    if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
    std::unique_lock lk(c_.inM_);
    c_.inCv_.wait(lk, [this] { return c_.closed_ || !c_.pending_.empty(); });
    if (c_.pending_.empty()) return traits_type::eof();   // closed: getline fails, the command gives up
    current_ = std::move(c_.pending_.front()) + "\n";
    c_.pending_.pop_front();
    setg(current_.data(), current_.data(), current_.data() + current_.size());
    return traits_type::to_int_type(*gptr());
}

Console::OutBuf::int_type Console::OutBuf::overflow(int_type ch) {
    if (ch != traits_type::eof()) {
        char c = (char)ch;
        c_.append(&c, 1);
    }
    return ch;
}

std::streamsize Console::OutBuf::xsputn(const char* s, std::streamsize n) {
    c_.append(s, (size_t)n);
    return n;
}

void Console::run(std::stop_token st) {
    std::istream in(&inBuf_);
    std::ostream out(&outBuf_);
    redirectThisThread(&out, &in, true);
    std::mt19937 rng(makeSeed() ^ 0x27d4eb2fu);

    out << "The same commands as tokenrush-cli. Type 'help' to see them. Esc goes back to the other tabs.\n";
    bool blankLine = false;
    while (!st.stop_requested()) {
        out << (blankLine ? "\n" : "") << promptText(s_) << std::flush;
        blankLine = true;
        std::string line;
        if (!std::getline(in, line)) break;

        Outcome result = runCommand(s_, rng, line, profiles_);
        switch (result) {
            case Outcome::Empty: blankLine = false; break;
            case Outcome::Clear: {
                std::lock_guard lk(outM_);
                out_.clear();
                partial_.clear();
                blankLine = false;
                break;
            }
            case Outcome::Quit:
            case Outcome::Logout:
                if (onExit_) onExit_(result);   // the TUI closes; stop() then ends this thread
                break;
            case Outcome::Done: break;
        }
    }
    redirectThisThread(nullptr, nullptr, false);
}

}  // namespace tui
