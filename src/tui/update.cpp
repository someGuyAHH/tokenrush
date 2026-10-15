#include "update.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace tui {

namespace {

// Never destroyed: the check runs on a detached thread that may still be waiting on git when
// the game quits, and it must not find this gone.
struct Check {
    std::mutex m;
    std::optional<UpdateInfo> found;
    std::atomic<bool> done{false};
    std::atomic<bool> requested{false};
    std::atomic<bool> dismissed{false};
};
Check& check() {
    static Check* c = new Check;
    return *c;
}

// Runs `git -C <repo> <args>` and collects what it prints. False if git fails or isn't there.
bool git(const std::string& repo, const std::string& args, std::string& out) {
#ifdef _WIN32
    std::string cmd = "git -C \"" + repo + "\" " + args + " 2>nul";
    FILE* p = _popen(cmd.c_str(), "r");
#else
    std::string cmd = "git -C '" + repo + "' " + args + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
#endif
    if (!p) return false;
    out.clear();
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
#ifdef _WIN32
    return _pclose(p) == 0;
#else
    return pclose(p) == 0;
#endif
}

int count(const std::string& repo, const std::string& range) {
    std::string out;
    if (!git(repo, "rev-list --count " + range, out)) return -1;
    try { return std::stoi(out); } catch (...) { return -1; }
}

void run(std::string repo) {
    std::optional<UpdateInfo> found;
    std::string out;
    // Fetch what the current branch follows (main, for a plain clone). Offline, or no git: no update.
    if (git(repo, "fetch --quiet", out)) {
        int behind = count(repo, "HEAD..@{u}");
        if (behind > 0) {
            UpdateInfo u;
            u.behind = behind;
            u.diverged = count(repo, "@{u}..HEAD") > 0;
            if (git(repo, "log -n 3 --format=%s HEAD..@{u}", out)) {
                std::istringstream lines(out);
                for (std::string line; std::getline(lines, line);)
                    if (!line.empty()) u.titles.push_back(line);
            }
            if (git(repo, "status --porcelain --untracked-files=no", out)) u.edited = !out.empty();
            found = u;
        }
    }
    Check& c = check();
    {
        std::lock_guard lk(c.m);
        c.found = found;
    }
    c.done = true;
}

}  // namespace

void startUpdateCheck() {
    const char* repo = std::getenv("TOKENRUSH_REPO");   // set by play.py, and only by it
    if (!repo || !*repo) { check().done = true; return; }
    std::thread(run, std::string(repo)).detach();
}

bool updateCheckDone() { return check().done; }

std::optional<UpdateInfo> updateAvailable() {
    Check& c = check();
    if (!c.done) return std::nullopt;
    std::lock_guard lk(c.m);
    return c.found;
}

bool updateReady() {
    std::optional<UpdateInfo> u = updateAvailable();
    return u && !u->diverged;
}

void requestUpdate() { check().requested = true; }
bool updateRequested() { return check().requested; }
void dismissUpdate() { check().dismissed = true; }
bool updateDismissed() { return check().dismissed; }

std::string updateSummary(const UpdateInfo& u) {
    std::string s = u.behind == 1 ? "1 new version" : std::to_string(u.behind) + " new versions";
    for (size_t i = 0; i < u.titles.size(); ++i) s += (i == 0 ? ": " : "; ") + u.titles[i];
    if ((int)u.titles.size() < u.behind) s += "; ...";
    return s;
}

}  // namespace tui
