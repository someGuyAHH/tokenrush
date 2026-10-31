#include "session.hpp"

#include "contracts.hpp"
#include "crew.hpp"
#include "game.hpp"
#include "profiles.hpp"
#include "save.hpp"
#include "util.hpp"
#include "workers.hpp"

ArgsResult parseLaunchArgs(int argc, char** argv, const std::string& program, LaunchOptions& opt, std::string& message) {
    opt.dataRoot = defaultDataRoot();
    auto value = [&](int& i, const std::string& flag, std::string& out) {
        if (i + 1 >= argc) { message = flag + " needs a value after it."; return false; }
        out = argv[++i];
        return true;
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i], v;
        if (a == "--no-color") {
            opt.noColor = true;
        } else if (a == "--no-update-check") {
            opt.noUpdateCheck = true;
        } else if (a == "--profile") {
            if (!value(i, a, v)) return ArgsResult::Error;
            opt.profile = v;
        } else if (a == "--data-dir") {
            if (!value(i, a, v)) return ArgsResult::Error;
            opt.dataRoot = v;
        } else if (a == "--save-dir") {
            if (!value(i, a, v)) return ArgsResult::Error;
            opt.saveDir = v;
        } else if (a == "--help" || a == "-h") {
            message = "Usage: " + program + " [--profile <name>] [--data-dir <folder>] [--save-dir <folder>] [--no-color] [--no-update-check]\n"
                      "  --profile          play this profile without the picker (created if it doesn't exist)\n"
                      "  --data-dir         where profiles live (default: " + defaultDataRoot().string() + ")\n"
                      "  --save-dir         play straight from this folder, ignoring profiles\n"
                      "  --no-update-check  started from play.py, don't look for updates on GitHub";
            return ArgsResult::Help;
        } else {
            message = "Unknown option: " + a + "  (try --help)";
            return ArgsResult::Error;
        }
    }
    return ArgsResult::Ok;
}

bool prepareSaveDir(const std::filesystem::path& dir, std::string& why) {
    setSaveDir(dir);
    if (ensureSaveDir(why)) return true;
    why = "Can't create the save folder " + dir.string() + ": " + why;
    return false;
}

// Nothing played at all: what a new Infected profile's first save looks like.
static bool isBlank(const GameState& g) {
    return g.tokens == 0 && g.shards == 0 && g.lifetimeEarned == 0 && g.playSeconds == 0 && g.manualMines == 0 &&
           g.pickaxe + g.magnet + g.drone + g.bot == 0 && g.goalsDone == 0 && g.curiosOwned == 0 &&
           g.monolithStage == 0 && g.monolithProgress == 0 && g.orreryStage == 0 && g.orreryProgress == 0 &&
           g.crew.empty() && g.contracts.empty() && g.unlocked == 0;
}

StartReport startGame(Shared& s, std::mt19937& rng) {
    StartReport r;
    LoadResult loaded = loadGame(s.g);   // no workers yet, so no locking needed
    std::time_t t = nowSec();

    switch (loaded.status) {
        case LoadStatus::Ok:
            // A new Infected profile starts with a save that holds nothing but the strain.
            r.fresh = s.g.strain != Strain::None && isBlank(s.g);
            if (!loaded.notice.empty()) r.notes.push_back({Sev::Warn, loaded.notice});
            break;
        case LoadStatus::NoFile:
            break;
        case LoadStatus::Corrupt:
            r.notes.push_back({Sev::Bad, "Couldn't read " + savePath().string() + ": " + loaded.why});
            if (quarantineSave()) r.notes.push_back({Sev::Warn, "It was moved to " + badSavePath().string() + ". Starting a fresh game."});
            else                  r.notes.push_back({Sev::Warn, "It couldn't be backed up and will be overwritten. Starting a fresh game."});
            break;
    }

    if (r.fresh) {
        Strain strain = loaded.status == LoadStatus::Ok ? s.g.strain : Strain::None;
        s.g = GameState{};
        s.g.strain = strain;
        s.g.lastUpdate = t;
        if (strain != Strain::None)
            r.notes.push_back({Sev::Bad, std::string("This profile is INFECTED with ") + STRAINS[(int)strain].name + ": " +
                                         STRAINS[(int)strain].desc + ". Complete the Orrery to cure it."});
    } else {
        long long before = s.g.lifetimeEarned;
        // The streak ran out while you were away, and before the offline income: the Tuning Fork's
        // +30% is for while you keep a streak going, not for a whole night away after quitting mid-streak.
        normalizeStreak(s.g, t);
        applyOfflineEarnings(s.g, t);
        s.g.lastUpdate = t;
        if (s.g.event != EV_NONE && t >= s.g.eventEnds) { s.g.event = EV_NONE; s.g.eventEnds = 0; }
        if (s.g.lifetimeEarned > before)
            r.notes.push_back({Sev::Good, "Your drones earned " + compact(s.g.lifetimeEarned - before) + " tokens while you were away."});
    }

    checkGoals(s.g, s.log);   // old saves may already qualify for some goals
    fillOfferBoard(s.g, rng);
    fillCandidates(s.g, rng);
    return r;
}

void Workers::start(Shared& s) {
    threads_.emplace_back(worldThread, std::ref(s));
    threads_.emplace_back(marketThread, std::ref(s));
    threads_.emplace_back(automationThread, std::ref(s));
    threads_.emplace_back(crewThread, std::ref(s));
    threads_.emplace_back(autosaveThread, std::ref(s));
    threads_.emplace_back(exchangeThread, std::ref(s));
}

void Workers::stop() {
    for (std::jthread& t : threads_) t.request_stop();   // all at once, so they wind down in parallel
    for (std::jthread& t : threads_) if (t.joinable()) t.join();
    threads_.clear();
}
