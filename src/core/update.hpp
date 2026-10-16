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
