#pragma once
// Starting and stopping a game session: command-line options, loading the save, the
// background threads, and the shutdown order (the TUI's startup, kept out of the UI code).

#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "state.hpp"

struct LaunchOptions {
    std::filesystem::path dataRoot;   // where profiles live; defaults to defaultDataRoot()
    std::string profile;              // --profile: skip the picker (created if it doesn't exist)
    std::filesystem::path saveDir;    // --save-dir: play straight from this folder, no profiles
    bool noColor = false;
};

enum class ArgsResult { Ok, Help, Error };

// Understands --profile <name>, --data-dir <folder>, --save-dir <folder>, --no-color and --help.
// For Help and Error, `message` says what to print. `program` is the name in the usage line.
ArgsResult parseLaunchArgs(int argc, char** argv, const std::string& program, LaunchOptions& opt, std::string& message);

// Points the save system at `dir` and creates the folder. False (with why) if it can't.
bool prepareSaveDir(const std::filesystem::path& dir, std::string& why);

// How a session ended.
enum class SessionEnd { Quit, Logout };

// Loads the save (moving an unreadable one aside), applies offline earnings, and fills the
// contract board and the crew candidates. Call before starting the workers.
struct StartReport {
    bool fresh = true;   // no usable save: a brand new game
    Notes notes;         // what happened, for the front end to show
};
StartReport startGame(Shared& s, std::mt19937& rng);

// The background threads. stop() requests a stop and joins them all; call it BEFORE the
// final save so nothing changes after it.
class Workers {
public:
    void start(Shared& s);
    void stop();
    ~Workers() { stop(); }

private:
    std::vector<std::jthread> threads_;
};
