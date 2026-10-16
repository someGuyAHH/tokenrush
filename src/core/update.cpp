#include "update.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

namespace {

// Never destroyed: the checker runs on a detached thread that may still be waiting on git when
// the game quits, and it must not find this gone.
struct Check {
    std::mutex m;             // found
    std::mutex gitM;          // one check at a time (the background one and 'update')
    std::optional<UpdateInfo> found;
    std::string repo;         // set once, before the thread starts
    std::atomic<bool> enabled{false};
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

// "Update: gameplay" on a line of its own (any case, spaces allowed).
bool markedGameplay(const std::string& body) {
    std::istringstream lines(body);
    for (std::string l; std::getline(lines, l);) {
        std::string t;
        for (char ch : l) if (ch != ' ' && ch != '\t' && ch != '\r') t += (char)std::tolower((unsigned char)ch);
        if (t == "update:gameplay") return true;
    }
    return false;
}

// The summary of what's new, from the new commits (newest first).
UpdateInfo summarize(const std::vector<Commit>& incoming, int behind) {
    UpdateInfo u;
    u.behind = behind;
    for (const Commit& c : incoming) {
        if (u.titles.size() < 3) u.titles.push_back(c.title);
        if (c.gameplay) ++u.gameplay;
    }
    u.required = u.gameplay > 0;
    return u;
}

// git log lines: fields split by \x1f, commits by \x1e.
std::vector<Commit> readLog(const std::string& repo, const std::string& args) {
    std::vector<Commit> out;
    std::string text;
    if (!git(repo, "log --format=%h%x1f%ct%x1f%an%x1f%s%x1f%b%x1e " + args, text)) return out;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\x1e', at);
        if (end == std::string::npos) end = text.size();
        std::string rec = text.substr(at, end - at);
        at = end + 1;
        while (!rec.empty() && (rec.front() == '\n' || rec.front() == '\r')) rec.erase(0, 1);
        if (rec.empty()) continue;
        std::vector<std::string> f;
        size_t p = 0;
        for (size_t q; (q = rec.find('\x1f', p)) != std::string::npos; p = q + 1) f.push_back(rec.substr(p, q - p));
        f.push_back(rec.substr(p));
        if (f.size() < 4) continue;
        Commit c;
        c.hash = f[0];
        try { c.when = std::stoll(f[1]); } catch (...) {}
        c.author = f[2];
        c.title = f[3];
        if (f.size() > 4) {
            c.body = f[4];
            while (!c.body.empty() && (c.body.back() == '\n' || c.body.back() == '\r' || c.body.back() == ' ')) c.body.pop_back();
        }
        c.gameplay = markedGameplay(c.body);
        out.push_back(c);
    }
    return out;
}

// One check. False if it couldn't reach GitHub; then what an earlier check found still stands.
bool runCheck(std::optional<UpdateInfo>& result) {
    Check& c = check();
    std::lock_guard g(c.gitM);
    std::string out;
    // Fetch what the current branch follows (main, for a plain clone). Offline, or no git: no news.
    if (!git(c.repo, "fetch --quiet", out)) return false;
    std::optional<UpdateInfo> found;
    int behind = count(c.repo, "HEAD..@{u}");
    if (behind > 0) {
        UpdateInfo u = summarize(readLog(c.repo, "-n 200 HEAD..@{u}"), behind);
        u.diverged = count(c.repo, "@{u}..HEAD") > 0;
        if (git(c.repo, "status --porcelain --untracked-files=no", out)) u.edited = !out.empty();
        found = u;
    }
    {
        std::lock_guard lk(c.m);
        c.found = found;
    }
    result = found;
    return true;
}

void loop() {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> wait(UPDATE_RECHECK_MIN, UPDATE_RECHECK_MAX);
    for (;;) {
        std::optional<UpdateInfo> ignored;
        runCheck(ignored);
        check().done = true;
        std::this_thread::sleep_for(std::chrono::seconds(wait(rng)));   // the thread is detached: quitting just ends it
    }
}

}  // namespace

void startUpdateCheck() {
    Check& c = check();
    const char* repo = std::getenv("TOKENRUSH_REPO");   // set by play.py, and only by it
    if (!repo || !*repo) { c.done = true; return; }
    c.repo = repo;
    c.enabled = true;
    std::thread(loop).detach();
}

bool updatesEnabled() { return check().enabled; }
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

bool updateRequired() {
    std::optional<UpdateInfo> u = updateAvailable();
    return u && !u->diverged && u->required;   // (a copy with commits of its own can't just move ahead: never required)
}

bool checkUpdateNow(std::optional<UpdateInfo>& found) {
    if (!updatesEnabled()) return false;
    bool ok = runCheck(found);
    if (ok) check().done = true;
    return ok;
}

void requestUpdate() { check().requested = true; }
bool updateRequested() { return check().requested; }
void dismissUpdate() { check().dismissed = true; }
bool updateDismissed() { return check().dismissed; }

std::string updateSummary(const UpdateInfo& u) {
    std::string s = u.behind == 1 ? "1 new version" : std::to_string(u.behind) + " new versions";
    if (u.required) s += u.behind == 1 ? " (a gameplay change)" : u.gameplay == 1 ? " (one a gameplay change)"
                                                               : " (" + std::to_string(u.gameplay) + " of them gameplay changes)";
    for (size_t i = 0; i < u.titles.size(); ++i) s += (i == 0 ? ": " : "; ") + u.titles[i];
    if ((int)u.titles.size() < u.behind) s += "; ...";
    return s;
}

std::string updateRepo() {
    const char* repo = std::getenv("TOKENRUSH_REPO");
    return repo ? repo : "";
}

bool repoStatus(RepoStatus& out, int maxCommits, bool withFiles) {
    out = RepoStatus{};
    std::string repo = updateRepo();
    if (repo.empty()) { out.why = "not started through play.py"; return false; }
    Check& c = check();
    if (c.repo.empty()) c.repo = repo;   // (started with --no-update-check: the background check never ran)
    std::lock_guard g(c.gitM);
    std::string text;
    if (!git(repo, "fetch --quiet", text)) { out.why = "couldn't reach GitHub (offline, or git isn't working)"; return false; }
    auto line = [&](const std::string& args) {
        std::string s;
        if (!git(repo, args, s)) return std::string();
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    };
    out.branch = line("rev-parse --abbrev-ref HEAD");
    out.upstream = line("rev-parse --abbrev-ref @{u}");
    if (out.upstream.empty()) { out.why = "this copy's branch (" + out.branch + ") doesn't follow one on GitHub"; return false; }
    std::vector<Commit> head = readLog(repo, "-n 1 HEAD");
    if (!head.empty()) out.current = head[0];
    out.behind = std::max(0, count(repo, "HEAD..@{u}"));
    out.ahead = std::max(0, count(repo, "@{u}..HEAD"));
    std::vector<Commit> all = out.behind > 0 ? readLog(repo, "-n 200 HEAD..@{u}") : std::vector<Commit>{};
    for (const Commit& k : all) out.gameplay += k.gameplay;
    out.incoming.assign(all.begin(), all.begin() + std::min<size_t>(all.size(), (size_t)std::max(1, maxCommits)));
    out.edited = !line("status --porcelain --untracked-files=no").empty();
    if (withFiles && out.behind > 0) {
        std::istringstream lines(line("diff --name-status HEAD...@{u}"));
        for (std::string l; std::getline(lines, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            for (char& ch : l) if (ch == '\t') ch = ' ';
            if (!l.empty()) out.files.push_back(l);
        }
    }
    // What the background check would have found, so the log and the u key agree with this.
    std::optional<UpdateInfo> found;
    if (out.behind > 0) {
        UpdateInfo u = summarize(all, out.behind);
        u.diverged = out.ahead > 0;
        u.edited = out.edited;
        found = u;
    }
    {
        std::lock_guard lk(c.m);
        c.found = found;
    }
    c.done = true;
    return true;
}

namespace {

bool openRepo(std::string& repo, std::string& why) {
    repo = updateRepo();
    if (repo.empty()) why = "not started through play.py";
    return !repo.empty();
}

}  // namespace

bool repoHistory(std::vector<Commit>& out, int count, std::string& why) {
    std::string repo;
    if (!openRepo(repo, why)) return false;
    std::lock_guard g(check().gitM);
    out = readLog(repo, "-n " + std::to_string(std::max(1, count)) + " HEAD");
    if (out.empty()) why = "git couldn't read this copy's history";
    return !out.empty();
}

bool repoCommit(Commit& out, const std::string& ref, std::string& why) {
    std::string repo;
    if (!openRepo(repo, why)) return false;
    std::string target;
    if (ref.size() >= 2 && ref[0] == '~' && ref.size() <= 6 && std::all_of(ref.begin() + 1, ref.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; }))
        target = "HEAD" + ref;
    else if (ref.size() >= 4 && ref.size() <= 40 && std::all_of(ref.begin(), ref.end(), [](char c) { return std::isxdigit((unsigned char)c) != 0; }))
        target = ref;
    else { why = "'" + ref + "' isn't a version: give ~3 (three before yours) or a hash"; return false; }
    std::lock_guard g(check().gitM);
    std::vector<Commit> one = readLog(repo, "-n 1 " + target);
    if (one.empty()) { why = "no version " + ref + (target == ref ? " (try 'check' first, so GitHub's are fetched)" : ""); return false; }
    out = one[0];
    return true;
}

bool repoFind(std::vector<Commit>& out, std::vector<bool>& fresh, const std::string& words, int limit, std::string& why) {
    std::string repo;
    if (!openRepo(repo, why)) return false;
    bool safe = !words.empty() && std::all_of(words.begin(), words.end(), [](char c) {
        return std::isalnum((unsigned char)c) != 0 || c == ' ' || c == '-' || c == '_' || c == '.';
    });
    if (!safe) { why = "search for letters, digits and spaces (and - _ .)"; return false; }
    std::lock_guard g(check().gitM);
    std::string n = "-n " + std::to_string(std::max(1, limit));
    std::string grep = " -i --fixed-strings --grep=\"" + words + "\" ";
    out = readLog(repo, n + grep + "HEAD @{u}");
    if (out.empty()) out = readLog(repo, n + grep + "HEAD");   // (no upstream to look at)
    std::string text;
    std::vector<std::string> incoming;
    if (git(repo, "rev-list --abbrev-commit HEAD..@{u}", text)) {
        std::istringstream lines(text);
        for (std::string l; std::getline(lines, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            incoming.push_back(l);
        }
    }
    fresh.clear();
    for (const Commit& c : out)
        fresh.push_back(std::any_of(incoming.begin(), incoming.end(), [&](const std::string& h) { return h.rfind(c.hash, 0) == 0 || c.hash.rfind(h, 0) == 0; }));
    return true;
}
