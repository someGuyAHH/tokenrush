// TokenRush, command-line edition (C++20). Same game and same save as tokenrush-tui.
//
// Layout:
//   src/core  the game itself: state, rules, actions, market, contracts, crew, save, threads.
//             It never prints and never reads input.
//   src/cli   this command-line UI: parses commands, calls the core, prints colored results.
//   src/tui   the full-screen UI (FTXUI).
//
// Threads (all in src/core/workers.cpp, each ticking once per second; started by core/session):
//   main        - reads commands and does ALL printing (workers never touch std::cout)
//   world       - passive income, play time, streak expiry, random events, contracts, heat, goals
//   market      - the shard price
//   automation  - Mining Bot and Auto-Buyer
//   crew        - your hires
//   autosave    - saves every 30s
//
// Rules that keep this safe:
//   - All game state lives in Shared::g and is only touched while holding Shared::m.
//   - Workers report through Shared::log; main drains it right before each prompt.
//   - Never hold Shared::m while waiting on user input (getline).
//   - Lock order when both are needed: saveMtx -> m. Nobody takes them the other way.
//   - Shutdown: stop + join workers FIRST, then do the final save.
//
// Options (see core/session.cpp):
//   --profile <name>      play this profile without the picker
//   --data-dir <folder>   where profiles live (default: %LOCALAPPDATA%\TokenRush)
//   --save-dir <folder>   play straight from this folder, ignoring profiles
//   --no-color            plain text (setting the NO_COLOR environment variable does the same)
//
// Build: see README.md (build.ps1 on Windows, make on Linux/macOS).

#include <csignal>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>

#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#endif

#include "shell/commands.hpp"
#include "shell/dispatch.hpp"
#include "core/contracts.hpp"
#include "core/game.hpp"
#include "core/save.hpp"
#include "core/profiles.hpp"
#include "core/session.hpp"
#include "core/state.hpp"
#include "core/util.hpp"
#include "shell/term.hpp"

static volatile std::sig_atomic_t g_interrupted = 0;
static void onSigint(int) { g_interrupted = 1; }

// One game, from loading the save to the final save. `profile` is empty with --save-dir.
static SessionEnd runSession(const std::filesystem::path& dir, const std::string& profile) {
    std::string message;
    if (!prepareSaveDir(dir, message)) {
        std::cout << paint(Style::Bad, message) << "\n";
        return SessionEnd::Quit;
    }

    Shared s;
    std::mt19937 rng(makeSeed());
    StartReport start = startGame(s, rng);
    printNotes(start.notes);
    if (!profile.empty()) std::cout << "Playing as " << paint(Style::Bold, profile) << ".\n";
    if (start.fresh) {
        std::cout << paint(Style::Header, "Welcome to TokenRush.") << "\n";
        printHelp(s);
    } else {
        std::cout << "Loaded save. " << paint(Style::Muted, "(Idle earnings capped at 12h; the bot, crew, market and "
                                                            "contract timers only run while you play.)") << "\n";
        std::cout << "Type 'help' for commands, 'goals' for things to aim for.\n";
    }

#ifndef _WIN32
    // On POSIX a signal can land on any thread. Block SIGINT while spawning so the
    // workers inherit the blocked mask, then unblock it only in main.
    sigset_t sigintSet;
    sigemptyset(&sigintSet);
    sigaddset(&sigintSet, SIGINT);
    pthread_sigmask(SIG_BLOCK, &sigintSet, nullptr);
#endif

    Workers workers;
    workers.start(s);

#ifndef _WIN32
    pthread_sigmask(SIG_UNBLOCK, &sigintSet, nullptr);
#endif

    // `break` inside a switch only leaves the switch, so quitting flips this flag instead.
    bool running = true;
    SessionEnd end = SessionEnd::Quit;
    bool showPrompt = true;   // false right after an empty Enter we quietly undid
    bool blankLine = true;    // a blank line between one command's output and the next prompt
    size_t promptWidth = 0;
    while (running && !g_interrupted) {
        if (showPrompt) {
            printPending(s);
            std::string prompt = promptText(s);
            promptWidth = visibleWidth(prompt);
            std::cout << (blankLine ? "\n" : "") << prompt << std::flush;
        }
        showPrompt = true;
        blankLine = true;

        std::string line;
        if (!std::getline(std::cin, line)) { std::cout << "\n"; break; } // EOF or Ctrl+C

        switch (runCommand(s, rng, line, !profile.empty())) {
            case Outcome::Empty:
                // Enter on an empty line does nothing: put the cursor back after the prompt.
                // If background news arrived meanwhile, show it under a fresh prompt instead.
                if (cursorControl() && !s.log.hasPending()) {
                    returnToPrompt(promptWidth);
                    showPrompt = false;
                }
                blankLine = false;
                break;
            case Outcome::Clear:  clearScreen(); blankLine = false; break;
            case Outcome::Quit:   running = false; break;
            case Outcome::Logout: running = false; end = SessionEnd::Logout; break;
            case Outcome::Done:   break;
        }
    }

    // Single exit path: stop workers first, so nothing changes after the final save.
    workers.stop();

    printPending(s);
    bool saved = saveNow(s);
    if (end == SessionEnd::Logout) std::cout << (saved ? "Saved.\n" : paint(Style::Bad, "Save failed.") + "\n");
    else                           std::cout << (saved ? "Saved. Bye.\n" : paint(Style::Bad, "Save failed. Bye.") + "\n");
    return end;
}

int main(int argc, char** argv) {
    LaunchOptions opt;
    std::string message;
    switch (parseLaunchArgs(argc, argv, "tokenrush-cli", opt, message)) {
        case ArgsResult::Ok:    break;
        case ArgsResult::Help:  std::cout << message << "\n"; return 0;
        case ArgsResult::Error: std::cout << message << "\n"; return 2;
    }
    initTerminal(opt.noColor);

#ifndef _WIN32
    struct sigaction sa {};
    sa.sa_handler = onSigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART, so Ctrl+C makes a blocked getline fail instead of resuming
    sigaction(SIGINT, &sa, nullptr);
#else
    std::signal(SIGINT, onSigint); // on Windows, Ctrl+C makes getline fail on its own
#endif

    if (!opt.saveDir.empty()) {   // --save-dir: no profiles at all
        runSession(opt.saveDir, "");
        return 0;
    }

    Profiles profiles(opt.dataRoot);
    std::string name;
    if (!opt.profile.empty()) {
        name = profiles.find(opt.profile);
        if (name.empty()) {
            if (!profiles.create(opt.profile, message)) {
                std::cout << paint(Style::Bad, message) << "\n";
                return 2;
            }
            name = profiles.find(opt.profile);
            std::cout << "Created profile " << paint(Style::Bold, name) << ".\n";
        }
    }

    // Pick a profile, play it, and come back here after 'logout'.
    while (!g_interrupted) {
        if (name.empty()) name = pickProfile(profiles);
        if (name.empty() || g_interrupted) break;
        if (runSession(profiles.dirOf(name), name) == SessionEnd::Quit) break;
        name.clear();
    }
    return 0;
}
