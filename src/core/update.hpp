#pragma once

// Updates. The game can't rebuild itself while it runs (Windows won't overwrite a running .exe),
// so play.py does that part: it starts the game with TOKENRUSH_REPO set to the git checkout, the
// game checks that checkout against GitHub in the background (at start, then every 5 to 10
// minutes) and asks, and on a yes it quits with UPDATE_EXIT_CODE. play.py then pulls, builds and
// starts it again. The Console's 'update' checks right away.
//
// Started any other way (or with --no-update-check) the game never looks.

#include <optional>
#include <string>
#include <vector>

inline constexpr int UPDATE_EXIT_CODE = 42;   // play.py knows this one
inline constexpr int UPDATE_RECHECK_MIN = 5 * 60, UPDATE_RECHECK_MAX = 10 * 60;   // seconds between checks

struct UpdateInfo {
    int behind = 0;                    // new commits on GitHub
    std::vector<std::string> titles;   // the newest few, newest first
    bool diverged = false;             // this copy has commits of its own: a pull can't just move ahead
    bool edited = false;               // uncommitted changes to tracked files (the pull may refuse)
};

void startUpdateCheck();                       // returns at once; git runs on its own thread, again and again
bool updatesEnabled();                         // started by play.py (and without --no-update-check)
bool updateCheckDone();                        // the first check has finished
std::optional<UpdateInfo> updateAvailable();   // from the latest check, if there's something new
bool updateReady();                            // ...and play.py can pull it (this copy hasn't diverged)
// Checks now, on the calling thread (a second or two of git), and returns what it found. False if
// the check itself failed (offline, no git): `found` is then left alone.
bool checkUpdateNow(std::optional<UpdateInfo>& found);
void requestUpdate();                          // the player said yes: main() returns UPDATE_EXIT_CODE
bool updateRequested();
void dismissUpdate();                          // "not now" on the profile picker: don't ask there again
bool updateDismissed();
std::string updateSummary(const UpdateInfo& u);   // "2 new versions: Wrap long log messages; ..."

// ----- 'check': a closer look, without installing anything -----
struct Commit {
    std::string hash;     // short
    long long when = 0;   // unix time it was committed
    std::string author;
    std::string title;    // the first line
    std::string body;     // the rest of the message (may be empty)
};
struct RepoStatus {
    std::string why;               // set when it couldn't look (no play.py, offline, no git)
    std::string branch, upstream;  // "main", "origin/main"
    Commit current;                // what you're running (the checkout's HEAD)
    std::vector<Commit> incoming;  // new on GitHub, newest first (up to the limit asked for)
    int behind = 0;                // all of them, past the limit too
    int ahead = 0;                 // commits of your own
    bool edited = false;           // uncommitted changes to tracked files
    std::vector<std::string> files;   // with `withFiles`: what the new versions change, "M src/core/game.cpp"
};
std::string updateRepo();   // the checkout play.py started the game from ("" if it didn't)
// Fetches and looks, on the calling thread (a second or two). False, with `why`, if it couldn't.
// Also refreshes what updateAvailable() says.
bool repoStatus(RepoStatus& out, int maxCommits, bool withFiles = false);

// Earlier versions, from the copy itself (no fetch). Each is false, with `why`, if it can't look.
bool repoHistory(std::vector<Commit>& out, int count, std::string& why);   // what you're running, then the ones before it
// One version: "~3" (three before yours) or a hash (4-40 hex digits), yours or GitHub's.
bool repoCommit(Commit& out, const std::string& ref, std::string& why);
// Versions whose message mentions `words` (letters, digits, spaces, - _ . only), newest first; `fresh`
// says which of them are new on GitHub (not installed yet).
bool repoFind(std::vector<Commit>& out, std::vector<bool>& fresh, const std::string& words, int limit, std::string& why);
