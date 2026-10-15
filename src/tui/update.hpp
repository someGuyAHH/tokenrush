#pragma once

// Updates. The game can't rebuild itself while it runs (Windows won't overwrite a running .exe),
// so play.py does that part: it starts the game with TOKENRUSH_REPO set to the git checkout, the
// game checks that checkout against GitHub in the background and asks, and on a yes it quits
// with UPDATE_EXIT_CODE. play.py then pulls, builds and starts it again.
//
// Started any other way (or with --no-update-check) the game never looks.

#include <optional>
#include <string>
#include <vector>

namespace tui {

inline constexpr int UPDATE_EXIT_CODE = 42;   // play.py knows this one

struct UpdateInfo {
    int behind = 0;                    // new commits on GitHub
    std::vector<std::string> titles;   // the newest few, newest first
    bool diverged = false;             // this copy has commits of its own: a pull can't just move ahead
    bool edited = false;               // uncommitted changes to tracked files (the pull may refuse)
};

void startUpdateCheck();                       // returns at once; git runs on its own thread
bool updateCheckDone();
std::optional<UpdateInfo> updateAvailable();   // once the check is done, if there's something new
bool updateReady();                            // ...and play.py can pull it (this copy hasn't diverged)
void requestUpdate();                          // the player said yes: main() returns UPDATE_EXIT_CODE
bool updateRequested();
void dismissUpdate();                          // "not now": don't ask again this session
bool updateDismissed();
std::string updateSummary(const UpdateInfo& u);   // "2 new versions: Wrap long log messages; ..."

}  // namespace tui
