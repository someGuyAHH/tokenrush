#pragma once
// The Console tab: the game's typed commands (src/shell), inside the TUI.
//
// It runs the command loop (shell/dispatch.cpp) on a thread of its own.
// That thread's out()/in() (shell/term.hpp) point at two streams owned by this class:
//   - input:  lines the player submits in the tab, handed over one at a time; a command that
//             asks a question (the shop menu, "type WIPE to confirm") simply waits for the next
//   - output: everything the commands print, colors included, kept as lines for the tab to draw
// The game itself is only touched through the commands, which lock it like a button click does.

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#include "core/state.hpp"
#include "shell/dispatch.hpp"

namespace tui {

class Console {
public:
    // `onExit` runs on the console thread when a command says quit or logout.
    Console(Shared& s, bool profiles, std::function<void(Outcome)> onExit);
    ~Console();

    void submit(const std::string& line);    // the player pressed Enter
    std::vector<std::string> lines(size_t last) const;   // the newest `last` finished lines
    size_t total() const;                    // finished lines ever written (keeps counting past what's kept)
    std::string partial() const;             // the unfinished last line (usually the prompt)
    std::vector<std::string> history() const;   // submitted lines, oldest first
    void stop();                             // ends the thread (also done by the destructor)

private:
    class InBuf : public std::streambuf {    // blocks until a line is submitted
    public:
        explicit InBuf(Console& c) : c_(c) {}
    protected:
        int_type underflow() override;
    private:
        Console& c_;
        std::string current_;
    };
    class OutBuf : public std::streambuf {   // collects lines
    public:
        explicit OutBuf(Console& c) : c_(c) {}
    protected:
        int_type overflow(int_type ch) override;
        std::streamsize xsputn(const char* s, std::streamsize n) override;
    private:
        Console& c_;
    };

    void run(std::stop_token st);
    void append(const char* s, size_t n);

    Shared& s_;
    bool profiles_;
    std::function<void(Outcome)> onExit_;

    mutable std::mutex inM_;
    std::condition_variable inCv_;
    std::deque<std::string> pending_;
    std::vector<std::string> history_;
    bool closed_ = false;

    mutable std::mutex outM_;
    std::deque<std::string> out_;
    std::string partial_;
    size_t total_ = 0;

    InBuf inBuf_{*this};
    OutBuf outBuf_{*this};
    std::jthread thread_;   // last: starts after everything above exists
};

}  // namespace tui
