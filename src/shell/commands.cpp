#include "commands.hpp"

#include <algorithm>
#include <bit>
#include <ctime>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "core/actions.hpp"
#include "core/content.hpp"
#include "core/choices.hpp"
#include "core/contracts.hpp"
#include "core/crew.hpp"
#include "core/disasters.hpp"
#include "core/game.hpp"
#include "core/market.hpp"
#include "core/save.hpp"
#include "core/update.hpp"
#include "core/util.hpp"
#include "term.hpp"

enum class Toggle { Show, On, Off, Invalid };

static Toggle parseToggle(const std::string& s) {
    if (s.empty()) return Toggle::Show;
    if (s == "on")  return Toggle::On;
    if (s == "off") return Toggle::Off;
    return Toggle::Invalid;
}

static std::string header(const std::string& title) { return paint(Style::Header, "=== " + title + " ==="); }


// ----- Info screens -----
bool commandAllowed(Shared& s, Cmd c) {
    int f = commandFeature(c);
    if (f < 0) return true;
    std::lock_guard lk(s.m);
    if (isUnlocked(s.g, (Feature)f)) return true;
    out() << paint(Style::Warn, lockedText(s.g, (Feature)f)) << "\n";
    return false;
}

std::string promptText(Shared& s) {
    std::lock_guard lk(s.m);
    const GameState& g = s.g;
    std::string prompt = promptOf(g);
    if (g.promptColorOwned && g.promptColorOn) {   // its own color, ended right after it (paintRgb resets)
        std::string mark = prompt, gap;
        while (!mark.empty() && mark.back() == ' ') { mark.pop_back(); gap += ' '; }   // the space after it stays plain
        prompt = paintRgb(g.promptColor, mark) + gap;
    }
    return "[" + paint(Style::Bold, TITLES[titleIndex(renownOf(g))].name) + "] " + prompt;
}

void printStatus(Shared& s) {
    std::lock_guard lk(s.m);
    GameState& g = s.g;
    std::time_t t = nowSec();
    normalizeStreak(g, t);
    EventFx fx = fxNow(g, t);
    int renown = renownOf(g);

    out() << header("STATUS") << "\n";
    out() << "Tokens: " << paint(Style::Tokens, num(g.tokens)) << "   (lifetime " << num(g.lifetimeEarned) << ")\n";
    out() << "Earning " << paint(Style::Tokens, perSecond(recentEarnRate(g))) << " tokens/s   (last "
              << EARN_WINDOW_SECONDS << "s)";
    if (std::string split = incomeSplit(g); !split.empty()) out() << paint(Style::Muted, "  " + split);
    out() << "\n";
    out() << "Est. Income " << paint(Style::Tokens, perSecond(estimatedIncome(g, nowSec()))) << " tokens/s"
          << paint(Style::Muted, "   (from your Drones, Bot and crew now, and your last " + std::to_string(EST_MINE_WINDOW) + "s of mining)") << "\n";
    if (isUnlocked(g, Feature::Market))
        out() << "Shards: " << paint(Style::Shards, num(g.shards)) << "   (price " << fixed(g.shardPrice, 1) << ")\n";
    out() << "Title:  " << paint(Style::Bold, TITLES[titleIndex(renown)].name) << "   Renown " << renown
              << "   Goals " << std::popcount(g.goalsDone) << "/" << GOAL_COUNT << "\n";
    if (g.strain != Strain::None) {
        const StrainInfo& st = STRAINS[(int)g.strain];
        if (g.cured) out() << paint(Style::Muted, std::string("Cured of ") + st.name + ".") << "\n";
        else out() << paint(Style::Bad, std::string("INFECTED: ") + st.name) << " - " << st.desc
                   << paint(Style::Muted, "  (complete the Orrery to cure it)") << "\n";
    }
    if (int next = nextUnlock(g); next >= 0) {
        const Title& t = TITLES[FEATURES[next].title];
        out() << paint(Style::Muted, std::string("Next unlock: ") + FEATURES[next].name + " at " + t.name + " (" +
                                         std::to_string(renown) + "/" + std::to_string(t.renown) + " renown)") << "\n";
    }

    if (g.streak > 0) {
        long long sinceMine = (long long)std::difftime(t, g.lastMine);
        long long left = std::max(0LL, streakTimeout(g) - sinceMine);
        out() << "Streak: " << g.streak << " (expires in " << left << "s, best " << g.bestStreak << ")\n";
    } else {
        out() << "Streak: 0 (best " << g.bestStreak << ")\n";
    }

    int ev = activeEvent(g, t);
    if (ev != EV_NONE) {
        out() << "Event:  " << paint(Style::Event, EVENTS[ev].name) << " - " << EVENTS[ev].desc
                  << " (" << (long long)std::difftime(g.eventEnds, t) << "s left)\n";
    }

    if (disasterPending(g))
        out() << paint(Style::Bad, "DANGER: " + std::string(DISASTERS[g.disaster.kind].name)) << " - " << disasterText(g)
              << paint(Style::Muted, "  (" + duration(g.disaster.secondsLeft) + ")") << "\n";
    if (anythingBroken(g)) {
        std::string list;
        for (const Repair& r : repairs(g)) list += (list.empty() ? "" : ", ") + r.what;
        out() << paint(Style::Bad, "Broken: ") << list << paint(Style::Muted, "  ('repair')") << "\n";
    }
    if (g.gadgetsUnplugged > 0) {
        out() << paint(Style::Warn, "Gadgets unplugged") << " for another " << duration(g.gadgetsUnplugged) << paint(Style::Muted, "  (they do nothing meanwhile)") << "\n";
    }
    if (g.dronesDown > 0)
        out() << paint(Style::Warn, num(g.dronesDown) + " Drones burnt out") << paint(Style::Muted, ", back in " + duration(g.dronesDownSecs)) << "\n";
    if (g.machinesPaused > 0) out() << paint(Style::Warn, "Breakers pulled") << paint(Style::Muted, ": Drones and Bot back in " + duration(g.machinesPaused)) << "\n";
    if (g.gadgetsUnplugged > 0) out() << paint(Style::Warn, "Gadgets unplugged") << paint(Style::Muted, ", back in " + duration(g.gadgetsUnplugged)) << "\n";
    if (hasChoice(g))
        out() << "Offer:  " << paint(Style::Event, choiceText(g)) << paint(Style::Muted, "  (yes/no, " + std::to_string(g.choice.secondsLeft) + "s)") << "\n";
    if (g.race.stake > 0)
        out() << "Race:   " << (g.manualMines - g.race.startMines) << "/" << g.race.target << " mines, "
              << g.race.secondsLeft << "s left (" << num(g.race.stake) << " tokens on it)\n";
    if (g.investment.amount > 0)
        out() << "Loan:   " << num(g.investment.amount) << " tokens with the Investor, due back in " << duration(g.investment.secondsLeft) << "\n";

    if (isUnlocked(g, Feature::Contracts)) {
        out() << "Heat:   " << paint(Style::Heat, heatBar(g.heat, MAX_HEAT)) << " " << g.heat << "/" << MAX_HEAT;
        if (g.heat >= auditMinHeat(g)) out() << paint(Style::Warn, "  (audits possible)");
        out() << "\n";
    }

    for (const Contract& c : g.contracts) {
        long long p = std::min(contractProgress(g, c), contractGoal(c));
        out() << "Job:    " << contractText(c) << "  " << bar(p, contractGoal(c)) << " " << progressText(g, c)
                  << "  " << paint(c.secondsLeft <= CONTRACT_WARN_AT ? Style::Warn : Style::Plain,
                                   duration(c.secondsLeft) + " left") << "\n";
    }
    if (isUnlocked(g, Feature::Crew)) {
        out() << "Crew:   " << g.crew.size() << "/" << crewCap(g);
        for (const CrewMember& c : g.crew)
            out() << "  " << c.name << " (" << ROLES[(int)c.role].name << " L" << c.level << (c.stars ? " " + starsText(c) : "") << ")";
        out() << "\n";
    }

    auto ups = upgrades(g);   // (the costs include the Rust strain, if any)
    out() << paint(Style::Header, "Upgrades:") << "\n";
    out() << "  Pickaxe lvl " << g.pickaxe << "  (base mine +" << g.pickaxe << ")"
              << "  next cost: " << num(ups[0].cost) << "\n";
    out() << "  Magnet  lvl " << g.magnet << "  next cost: " << num(ups[1].cost) << "\n";
    out() << "  Drone   lvl " << g.drone << "  passive: " << perSecond(passiveRatePerSec(g) * fx.passive)
              << " tok/s  x" << compact((long long)milestoneMult(g.drone)) << " (x2 more at " << compact(nextMilestone(g.drone)) << ")"
              << "  next cost: " << num(ups[2].cost) << "\n";
    out() << "  Bot     lvl " << g.bot;
    if (g.bot > 0) out() << "  mines every " << botIntervalOf(g) << "s  x" << compact((long long)milestoneMult(g.bot))
                         << " (x2 more at " << compact(nextMilestone(g.bot)) << ")";
    else           out() << "  (not built)";
    out() << "  next cost: " << num(ups[3].cost) << "\n";

    out() << "Auto-Buyer: ";
    if (!g.autoUnlocked) out() << "locked (shop, " << num(AUTOBUY_LICENSE_COST) << " tokens)\n";
    else if (g.autoOn)   out() << paint(Style::Good, "ON") << " (buys cheapest upgrade at " << AUTOBUY_RESERVE << "x its cost)\n";
    else                 out() << "OFF\n";

    if (isUnlocked(g, Feature::Curios)) out() << "Curios: " << std::popcount(g.curiosOwned) << "/" << CURIO_COUNT << "   ";
    if (isUnlocked(g, Feature::Monolith)) {
        out() << "Monolith: ";
        if (g.monolithStage >= MONO_STAGE_COUNT) out() << "complete";
        else out() << "stage " << (g.monolithStage + 1) << "/" << MONO_STAGE_COUNT << " ("
                       << MONO[g.monolithStage].name << ") " << num(g.monolithProgress) << " / "
                       << num(MONO[g.monolithStage].cost);
    }
    if (isUnlocked(g, Feature::Curios) || isUnlocked(g, Feature::Monolith)) out() << "\n";

    long long ls = s.lastSave.load();
    out() << paint(Style::Muted, "Last save: " + (ls == 0 ? std::string("not yet this session")
                                                             : std::to_string((long long)std::difftime(t, (std::time_t)ls)) + "s ago"))
              << "\n";
}

void doGoals(Shared& s) {
    std::lock_guard lk(s.m);
    const GameState& g = s.g;
    int renown = renownOf(g);
    int ti = titleIndex(renown);

    out() << header("GOALS (" + std::to_string(std::popcount(g.goalsDone)) + "/" + std::to_string(GOAL_COUNT) + " done)") << "\n";
    for (int i = 0; i < GOAL_COUNT; ++i) {
        const Goal& goal = GOALS[i];
        bool done = hasBit(g.goalsDone, i);
        std::string row = std::string(done ? "[x] " : "[ ] ") + padRight(goal.name, 16) + padRight(goal.desc, 36);
        int needs = featureOfStat(goal.stat);
        if (done) {
            out() << "  " << paint(Style::Good, row + "+" + std::to_string(goal.renown) + " renown") << "\n";
        } else if (needs >= 0 && !isUnlocked(g, (Feature)needs)) {
            out() << "  " << paint(Style::Muted, row + "opens at " + TITLES[FEATURES[needs].title].name) << "\n";
        } else {
            long long cur = std::min(statValue(g, goal.stat), goal.target);
            out() << "  " << row << bar(cur, goal.target) << " " << num(cur) << "/" << num(goal.target) << "\n";
        }
    }

    // Secret goals: only a hint until you find them.
    out() << "\n" << paint(Style::Header, "Secret goals") << paint(Style::Muted, "  (" + std::to_string(std::popcount(g.secretsDone)) + "/" +
                                                                  std::to_string(SECRET_COUNT) + " found; they pay shards, not renown)") << "\n";
    for (int i = 0; i < SECRET_COUNT; ++i) {
        const SecretGoal& sg = SECRETS[i];
        if (hasBit(g.secretsDone, i))
            out() << "  " << paint(Style::Good, "[x] " + padRight(sg.name, 22) + padRight(sg.desc, 58) + "+" + std::to_string(sg.shards) + " shards") << "\n";
        else
            out() << "  " << paint(Style::Muted, "[?] " + padRight("???", 22) + sg.hint) << "\n";
    }

    out() << "\nRenown: " << renown << "   Title: " << paint(Style::Bold, TITLES[ti].name);
    if (ti + 1 < TITLE_COUNT) out() << "   (next: " << TITLES[ti + 1].name << " at " << TITLES[ti + 1].renown << ")";
    out() << "\n\n" << paint(Style::Header, "Titles") << "\n";
    for (int t = 0; t < TITLE_COUNT; ++t) {
        std::string opens;
        for (int f = 0; f < FEATURE_COUNT; ++f)
            if (FEATURES[f].title == t) opens += (opens.empty() ? "" : ", ") + std::string(FEATURES[f].name);
        std::string tail = t == TITLE_COUNT - 1 ? "every goal done" : opens.empty() ? "" : "opens " + opens;
        std::string row = std::string(t == ti ? "> " : "  ") + padRight(TITLES[t].name, 16) +
                          padLeft(std::to_string(TITLES[t].renown), 3) + (tail.empty() ? "" : "   " + tail);
        out() << paint(t <= ti ? Style::Good : Style::Muted, row) << "\n";
    }
}

void doLog(Shared& s, const std::string& args) {
    // "log", "log 50", "log bad", "log 100 event", in any order
    size_t n = 20;
    int filter = -1;   // a Sev, or -1 for everything
    static const char* const sevNames[] = {"info", "good", "bad", "warn", "event"};
    std::stringstream ss(lower(args));
    std::string word;
    while (ss >> word) {
        long long v;
        if (parseLongLong(word, v) && v > 0) { n = (size_t)std::min(v, 1000LL); continue; }
        if (word == "all") continue;
        if (word == "warning" || word == "warnings") word = "warn";
        if (word.size() > 1 && word.back() == 's') word.pop_back();   // "events" works too
        int found = -1;
        for (int i = 0; i < 5; ++i)
            if (word == sevNames[i]) found = i;
        if (found < 0) { out() << paint(Style::Warn, "Usage: log [n] [good|bad|warn|event|info]") << "\n"; return; }
        filter = found;
    }

    std::vector<LogEntry> entries = s.log.recent(1000);
    if (filter >= 0) std::erase_if(entries, [filter](const LogEntry& e) { return (int)e.sev != filter; });
    if (entries.size() > n) entries.erase(entries.begin(), entries.end() - (std::ptrdiff_t)n);
    if (entries.empty()) {
        out() << (filter >= 0 ? "No " + std::string(sevNames[filter]) + " messages this session.\n"
                              : std::string("Nothing has happened in the background yet.\n"));
        return;
    }
    out() << header("LOG (last " + std::to_string(entries.size()) + (filter >= 0 ? " " + std::string(sevNames[filter]) : "") + ")") << "\n";
    for (const LogEntry& e : entries) printEntry(e);
}

// ----- Actions -----
void doMine(Shared& s, std::mt19937& rng) {
    MineResult r;
    {
        std::lock_guard lk(s.m);
        r = mine(s.g, nowSec(), rng);
    }

    out() << "You mined " << paint(Style::Tokens, "+" + num(r.gain)) << " tokens"
              << paint(Style::Muted, "  (base " + std::to_string(r.base) + ", x" + fixed(r.mult, 2) + ")");
    if (r.jackpot)      out() << "  " << paint(Style::Event, "[JACKPOT!]");
    else if (r.bonus)   out() << "  " << paint(Style::Good, "[bonus]");
    if (r.meteor)       out() << "  " << paint(Style::Event, std::string("[") + EVENTS[EV_METEOR].name + " x2]");
    out() << "\n";
}

void doAuto(Shared& s, const std::string& arg) {
    Notes out;
    {
        std::lock_guard lk(s.m);
        switch (parseToggle(arg)) {
            case Toggle::Show:
                if (!s.g.autoUnlocked) out = setAuto(s.g, false);   // explains how to unlock it
                else out = {{Sev::Info, std::string("Auto-Buyer is ") + (s.g.autoOn ? "ON" : "OFF") +
                                        ". Use 'auto on' or 'auto off'."}};
                break;
            case Toggle::On:      out = setAuto(s.g, true);  break;
            case Toggle::Off:     out = setAuto(s.g, false); break;
            case Toggle::Invalid: out = {{Sev::Warn, "Usage: auto [on|off]"}}; break;
        }
    }
    printNotes(out);
}

void doSilence(Shared& s, const std::string& args, bool silence) {
    std::lock_guard lk(s.m);   // it only prints and flips bits: holding the lock is fine
    GameState& g = s.g;
    std::stringstream ss(lower(args));
    std::vector<std::string> asked;
    for (std::string w; ss >> w;) asked.push_back(w);
    std::string unknown;
    for (const std::string& w : asked) {
        if (w == "all") {
            g.silenced = silence ? ((1u << LOG_KIND_COUNT) - 1) & ~1u : 0u;
            continue;
        }
        int found = -1;
        for (int i = 1; i < LOG_KIND_COUNT; ++i)
            if (w == LOG_KINDS[i].id || w + "s" == LOG_KINDS[i].id || w == std::string(LOG_KINDS[i].id) + "s") found = i;
        if (found < 0) { unknown += (unknown.empty() ? "" : ", ") + w; continue; }
        if (silence) g.silenced |= 1u << found;
        else g.silenced &= ~(1u << found);
    }
    if (!unknown.empty()) out() << paint(Style::Warn, "No kind of message called " + unknown + ".") << "\n";
    if (!asked.empty() && unknown.empty()) out() << paint(Style::Good, silence ? "Silenced." : "Back in the log.") << "\n";
    out() << paint(Style::Header, "=== LOG ===") << paint(Style::Muted, "   silenced kinds stay out of the log pane, but 'log' still has them") << "\n";
    for (int i = 1; i < LOG_KIND_COUNT; ++i) {
        bool off = g.silenced >> i & 1u;
        out() << "  " << padRight(LOG_KINDS[i].id, 11) << (off ? paint(Style::Muted, padRight("silenced", 10)) : paint(Style::Good, padRight("shown", 10)))
              << paint(Style::Muted, LOG_KINDS[i].what) << "\n";
    }
    out() << paint(Style::Muted, "'silence market bot' or 'silence all'; 'unsilence market' or 'unsilence all'. Disasters, offers, audits and "
                                 "jobs done or failed always show.") << "\n";
}

void doWipe(Shared& s) {
    out() << paint(Style::Warn, "Type WIPE to confirm (anything else cancels): ") << std::flush;
    std::string line;
    if (!std::getline(in(), line)) return;
    if (line != "WIPE") { out() << "Cancelled.\n"; return; }

    // Hold saveMtx so an in-flight autosave can't write the old state back after we delete it.
    std::lock_guard sl(s.saveMtx);
    std::lock_guard lk(s.m);
    deleteSaveFiles();
    resetGame(s.g, nowSec());
    out() << "Progress wiped.\n";
}

bool doUpdate() {
    if (!updatesEnabled()) {
        out() << "This run doesn't check for updates. Start the game with " << paint(Style::Bold, "python play.py")
              << " and it can (after an update that failed, play.py starts it once without checking).\n";
        return false;
    }
    std::optional<UpdateInfo> u;
    if (!checkUpdateNow(u)) {
        u = updateAvailable();   // what the last check in the background found, if anything
        out() << paint(Style::Warn, "Couldn't reach GitHub (offline, or git isn't working).") << "\n";
        if (!u) return false;
        out() << "The last check found an update, though.\n";
    }
    if (!u) { out() << paint(Style::Good, "Up to date.") << "\n"; return false; }
    out() << paint(Style::Event, "Update: " + updateSummary(*u)) << "\n";
    if (u->diverged) {
        out() << "This copy has commits of its own, so it can't just move ahead: pull it yourself (git pull), then build.\n";
        return false;
    }
    if (u->edited) out() << paint(Style::Muted, "You've changed some of the code; if the update touches the same files, play.py stops and says so.") << "\n";
    out() << "Saving and quitting: play.py pulls it, builds it and starts the game again.\n";
    requestUpdate();
    return true;
}

// ----- check: what's new on GitHub, without installing it -----
namespace {

// "2h ago (Oct 16, 14:03)" for a unix time.
std::string committedText(long long when) {
    std::time_t now = std::time(nullptr), t = (std::time_t)when;
    long long ago = std::max(0LL, (long long)now - when);
    std::string rel = ago < 90 ? "just now" : ago < 5400 ? std::to_string((ago + 30) / 60) + "m ago"
                    : ago < 2 * 86400 ? std::to_string((ago + 1800) / 3600) + "h ago" : std::to_string((ago + 43200) / 86400) + " days ago";
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%b %d, %H:%M", &tm);
    return rel + " (" + buf + ")";
}

void checkUsage() {
    out() << paint(Style::Header, "=== CHECK ===") << paint(Style::Muted, "   what's new on GitHub; nothing gets installed ('update' does that)") << "\n";
    const std::pair<const char*, const char*> rows[] = {
        {"check", "up to date or not, and the newest change"},
        {"check count", "how many new versions (and how many of your own)"},
        {"check list [n]", "the new versions: when, and what (10 unless you say)"},
        {"check titles [n]", "just what they're called"},
        {"check dates [n]", "just when they were made"},
        {"check show <n>", "the whole message of one (n from the list, or its hash)"},
        {"check files", "which files the new versions change"},
        {"check current", "the version you're running"},
        {"check history [n]", "your version and the ones before it (10 unless you say)"},
        {"check show ~3", "an earlier one: ~1 is the one before yours (a hash works too)"},
        {"check find <words>", "versions whose message mentions them, yours and GitHub's"},
    };
    for (auto [cmd, what] : rows) out() << "  " << padRight(cmd, 20) << paint(Style::Muted, what) << "\n";
}

}  // namespace

void doCheck(const std::string& args) {
    std::stringstream ss(lower(trim(args)));
    std::string sub, arg;
    ss >> sub >> arg;
    if (sub == "help" || sub == "?") { checkUsage(); return; }
    static const char* const SUBS[] = {"", "count", "n", "list", "log", "new", "titles", "messages", "names", "dates", "when",
                                       "show", "message", "files", "changes", "current", "version", "me",
                                       "history", "past", "earlier", "old", "find", "search"};
    if (std::find(std::begin(SUBS), std::end(SUBS), sub) == std::end(SUBS)) {
        out() << paint(Style::Warn, "No 'check " + sub + "'.") << "\n";
        checkUsage();
        return;
    }
    long long limit = 10;
    if (!arg.empty() && sub != "show" && sub != "message" && sub != "find" && sub != "search") {
        if (!parseLongLong(arg, limit) || limit < 1) { out() << "'" << arg << "' isn't a number of versions.\n"; return; }
        limit = std::min(limit, 100LL);
    }
    if (updateRepo().empty()) {
        out() << "This run can't look: start the game with " << paint(Style::Bold, "python play.py") << " (from the git checkout) and it can.\n";
        return;
    }
    std::string why;
    if (sub == "history" || sub == "past" || sub == "earlier" || sub == "old") {
        std::vector<Commit> list;
        if (!repoHistory(list, (int)limit + 1, why)) { out() << paint(Style::Warn, "Couldn't look: " + why + ".") << "\n"; return; }
        out() << "Your version and the " << (list.size() - 1) << (list.size() == 2 ? " before it" : " before it") << ", newest first:\n";
        for (size_t i = 0; i < list.size(); ++i) {
            const Commit& c = list[i];
            std::string tag = i == 0 ? "now" : "~" + std::to_string(i);
            out() << padLeft(tag, 5) << "  " << paint(Style::Muted, c.hash) << "  " << padRight(committedText(c.when), 26)
                  << (c.gameplay ? paint(Style::Warn, "gameplay ") : "") << (i == 0 ? paint(Style::Bold, c.title) : c.title) << "\n";
        }
        out() << paint(Style::Muted, "'check show ~2' for the whole message of one; 'check history 30' goes further back.") << "\n";
        return;
    }
    if (sub == "find" || sub == "search") {
        std::string words = trim(trim(args).substr(sub.size()));
        if (words.empty()) { out() << "Find what? 'check find tasks', for example.\n"; return; }
        std::vector<Commit> list;
        std::vector<bool> fresh;
        if (!repoFind(list, fresh, words, 30, why)) { out() << paint(Style::Warn, "Couldn't look: " + why + ".") << "\n"; return; }
        if (list.empty()) { out() << "No version mentions '" << words << "'.\n"; return; }
        out() << list.size() << (list.size() == 1 ? " version mentions '" : " versions mention '") << words << "'" << (list.size() >= 30 ? " (the newest 30)" : "") << ":\n";
        for (size_t i = 0; i < list.size(); ++i) {
            const Commit& c = list[i];
            out() << "  " << (fresh[i] ? paint(Style::Event, "new ") : "    ") << paint(Style::Muted, c.hash) << "  "
                  << padRight(committedText(c.when), 26) << (c.gameplay ? paint(Style::Warn, "gameplay ") : "") << c.title << "\n";
        }
        out() << paint(Style::Muted, "'new' ones aren't installed yet. 'check show <hash>' for the whole message.") << "\n";
        return;
    }
    RepoStatus st;
    bool showing = sub == "show" || sub == "message";
    int wanted = showing ? 100 : sub.empty() ? 1 : (int)limit;
    if (!repoStatus(st, wanted, sub == "files" || sub == "changes") && !showing) {   // (an earlier version can be shown offline)
        out() << paint(Style::Warn, "Couldn't check: " + st.why + ".") << "\n";
        return;
    }
    auto versions = [&st](int n) {
        std::string s = n == 1 ? std::string("1 new version") : std::to_string(n) + " new versions";
        if (st.gameplay > 0) s += n == 1 ? " (a gameplay change: needed to play)" : " (" + std::to_string(st.gameplay) + " gameplay: needed to play)";
        return s;
    };
    auto mark = [](const Commit& c) { return c.gameplay ? paint(Style::Warn, "gameplay ") : std::string(); };
    std::string where = st.upstream.empty() ? "GitHub" : st.upstream;

    if (sub == "current" || sub == "version" || sub == "me") {
        out() << "You're running " << paint(Style::Bold, st.current.hash) << " on " << st.branch << ", from " << committedText(st.current.when) << ":\n"
              << "  " << st.current.title << "\n";
        if (st.ahead > 0) out() << paint(Style::Muted, "  " + std::to_string(st.ahead) + " of this copy's commits aren't on " + where + ".") << "\n";
        if (st.edited) out() << paint(Style::Muted, "  Some of the code has been changed and not committed.") << "\n";
        out() << (st.behind == 0 ? paint(Style::Good, "Up to date.") : paint(Style::Event, versions(st.behind) + " on " + where + ".")) << "\n";
        return;
    }
    if (sub == "count" || sub == "n") {
        out() << (st.behind == 0 ? paint(Style::Good, "Up to date") : paint(Style::Event, versions(st.behind))) << " on " << where;
        if (st.ahead > 0) out() << "; " << st.ahead << " of your own that " << where << " doesn't have";
        out() << ".\n";
        return;
    }
    if (showing) {
        if (arg.empty()) { out() << "Show which? 'check show 1' (the newest new one), 'check show ~1' (the one before yours), or a hash.\n"; return; }
        const Commit* c = nullptr;
        long long n = 0;
        if (parseLongLong(arg, n) && arg.size() < 4) {
            if (n >= 1 && n <= (long long)st.incoming.size()) c = &st.incoming[(size_t)n - 1];
        } else {
            for (const Commit& k : st.incoming)
                if (k.hash.rfind(arg, 0) == 0 || arg.rfind(k.hash, 0) == 0) c = &k;
        }
        Commit any;
        if (!c) {   // an earlier one (~3), or any hash
            if (!repoCommit(any, arg, why)) { out() << paint(Style::Warn, "Can't show it: " + why + ".") << "\n"; return; }
            c = &any;
        }
        out() << paint(Style::Bold, c->hash) << paint(Style::Muted, "  by " + c->author + ", " + committedText(c->when))
              << (c->gameplay ? paint(Style::Warn, "  (gameplay update)") : "") << "\n"
              << "  " << paint(Style::Bold, c->title) << "\n";
        if (!c->body.empty()) {
            std::istringstream lines(c->body);
            for (std::string l; std::getline(lines, l);) {
                if (!l.empty() && l.back() == '\r') l.pop_back();
                out() << "  " << l << "\n";
            }
        }
        return;
    }
    if (st.behind == 0) {
        out() << paint(Style::Good, "Up to date") << " with " << where << paint(Style::Muted, " (you're on " + st.current.hash + ", " + committedText(st.current.when) + ")") << ".\n";
        return;
    }
    if (sub.empty()) {
        const Commit& c = st.incoming.front();
        out() << paint(Style::Event, versions(st.behind)) << " on " << where << ". The newest, " << committedText(c.when) << ":\n"
              << "  " << mark(c) << paint(Style::Bold, c.title) << "\n";
        if (st.ahead > 0) out() << paint(Style::Warn, "  This copy has commits of its own, so 'update' can't just move it ahead: pull it yourself.") << "\n";
        else out() << paint(Style::Muted, "  'update' installs it; 'check list' shows them all.") << "\n";
        return;
    }
    if (sub == "files" || sub == "changes") {
        out() << paint(Style::Event, versions(st.behind)) << " change " << st.files.size() << (st.files.size() == 1 ? " file" : " files") << ":\n";
        for (const std::string& f : st.files) {
            char k = f.empty() ? ' ' : f[0];
            std::string kind = k == 'A' ? "new     " : k == 'D' ? "gone    " : k == 'R' ? "renamed " : "changed ";
            out() << "  " << paint(k == 'A' ? Style::Good : k == 'D' ? Style::Bad : Style::Muted, kind) << trim(f.substr(1)) << "\n";
        }
        return;
    }
    out() << paint(Style::Event, versions(st.behind)) << " on " << where << (st.behind > (int)st.incoming.size() ? ", the newest " + std::to_string(st.incoming.size()) : "") << ":\n";
    for (size_t i = 0; i < st.incoming.size(); ++i) {
        const Commit& c = st.incoming[i];
        out() << padLeft(std::to_string(i + 1), 4) << ") " << paint(Style::Muted, c.hash) << "  ";
        if (sub == "titles" || sub == "messages" || sub == "names") out() << mark(c) << c.title;
        else if (sub == "dates" || sub == "when") out() << committedText(c.when) << (c.gameplay ? "  " + mark(c) : "");
        else out() << padRight(committedText(c.when), 26) << mark(c) << c.title;
        out() << "\n";
    }
    if (sub == "list" || sub == "log" || sub == "new") out() << paint(Style::Muted, "'check show <n>' for the whole message; 'update' installs them.") << "\n";
}

// ----- Disasters -----
void doRespond(Shared& s) {
    Notes notes;
    {
        std::lock_guard lk(s.m);
        notes = respondDisaster(s.g);
    }
    printNotes(notes);
}

void doRepair(Shared& s, const std::string& arg) {
    Notes notes;
    {
        std::lock_guard lk(s.m);
        std::vector<Repair> list = repairs(s.g);
        if (arg.empty()) {
            if (list.empty()) { out() << "Nothing needs repairing.\n"; return; }
            out() << header("REPAIRS") << paint(Style::Muted, fixerSoften(s.g) < 1.0 ? "   (your Fixers got you a discount)" : "") << "\n";
            for (size_t i = 0; i < list.size(); ++i) {
                const Repair& r = list[i];
                std::string cost = r.tokens > 0 ? paint(Style::Tokens, num(r.tokens) + " tokens") : paint(Style::Shards, count(r.shards, "shard"));
                out() << "  " << (i + 1) << ") " << padRight(r.what, 28) << padVisible(cost, 18) << paint(Style::Muted, r.effect) << "\n";
            }
            out() << "Use 'repair <n>' or 'repair all'.\n";
            return;
        }
        long long n = 0;
        if (arg == "all") notes = repair(s.g, -1);
        else if (parseLongLong(arg, n) && n >= 1) notes = repair(s.g, (int)n - 1);
        else notes = {{Sev::Warn, "Usage: repair [n|all]"}};
    }
    printNotes(notes);
}

// ----- Offers -----
void doAnswer(Shared& s, std::mt19937& rng, bool yes) {
    Notes notes;
    {
        std::lock_guard lk(s.m);
        notes = answerChoice(s.g, yes, rng);
    }
    printNotes(notes);
}

// ----- Stats -----
static std::string playTime(long long secs) {
    if (secs < 0) return "before records";
    long long h = secs / 3600, m = (secs / 60) % 60;
    return h > 0 ? std::to_string(h) + "h " + std::to_string(m) + "m" : std::to_string(m) + "m";
}

void doStats(Shared& s) {
    std::lock_guard lk(s.m);
    const GameState& g = s.g;
    auto row = [](const std::string& label, const std::string& value) { out() << "  " << padRight(label, 18) << value << "\n"; };

    out() << header("STATS") << "\n";
    out() << paint(Style::Bold, "Wealth") << "\n";
    row("Tokens", paint(Style::Tokens, num(g.tokens)));
    row("Earned in total", paint(Style::Tokens, num(g.lifetimeEarned)));
    row("Earning now", perSecond(recentEarnRate(g)) + "/s   (best " + perSecond((double)g.bestEarnRate) + "/s)");
    row("Est. Income", perSecond(estimatedIncome(g, nowSec())) + "/s" +
                           paint(Style::Muted, "   (the formulas: Drones, Bot, crew, and your last " + std::to_string(EST_MINE_WINDOW) + "s of mining)"));
    if (isUnlocked(g, Feature::Market)) row("Shards", paint(Style::Shards, num(g.shards)));

    out() << paint(Style::Bold, "Mining") << "\n";
    row("By hand", num(g.manualMines) + " times, " + num(g.jackpots) + " jackpots");
    row("Biggest jackpot", g.biggestJackpot > 0 ? num(g.biggestJackpot) : "none yet");
    row("Best streak", num(g.bestStreak));
    row("Mining Bot", num(g.botTotal) + " tokens");

    // Where the tokens came from, all time and over the last few seconds.
    out() << paint(Style::Bold, "Income by source") << paint(Style::Muted, "   (all time, and now)") << "\n";
    auto rates = recentSourceRates(g);
    long long sorted = 0;
    for (long long v : g.earnedBy) sorted = addTokens(sorted, v);
    double total = std::max(1.0, (double)g.lifetimeEarned);
    for (int i = 0; i < SOURCE_COUNT; ++i) {
        long long v = g.earnedBy[(size_t)i];
        if (v <= 0 && rates[(size_t)i] <= 0.0) continue;
        double share = (double)v / total;
        out() << "  " << padRight(sourceName((Source)i), 18) << paint(Style::Tokens, padLeft(compact(v), 7)) << "  "
              << bar(toTokens(share * 1000.0), 1000, 12) << " " << padLeft(percent(share, 0), 4)
              << paint(Style::Muted, "   now " + perSecond(rates[(size_t)i]) + "/s") << "\n";
    }
    if (g.lifetimeEarned > sorted)
        out() << "  " << padRight("Before records", 18) << paint(Style::Muted, padLeft(compact(g.lifetimeEarned - sorted), 7) +
                                                                  "  (earned before this version kept track)") << "\n";

    if (isUnlocked(g, Feature::Market) || isUnlocked(g, Feature::Contracts) || isUnlocked(g, Feature::Crew)) {
        out() << paint(Style::Bold, "Business") << "\n";
        if (isUnlocked(g, Feature::Market)) {
            row("Shards traded", num(g.shardsTraded));
            long long net = g.tokensFromSells - g.tokensOnBuys;
            row("Trading", (net >= 0 ? paint(Style::Good, "+" + num(net)) : paint(Style::Bad, "-" + num(-net))) +
                               " tokens net, " + num(g.tradeCost) + " lost to spread and price impact");
        }
        if (isUnlocked(g, Feature::Contracts)) {
            row("Contracts", num(g.contractsDone) + " done, " + num(g.contractsFailed) + " failed");
            row("Audits", num(g.audits));
        }
        if (isUnlocked(g, Feature::Crew)) {
            row("Crew", num(g.crewHired) + " hired, mined " + num(g.crewMined) + " tokens, " + num(g.expeditionsDone) + " expeditions");
            row("Disasters", num(g.disastersStopped) + " stopped, " + num(g.disastersHit) + " hit");
        }
    }

    if (!g.formerCrew.empty()) {
        out() << paint(Style::Bold, "Former crew") << paint(Style::Muted, "   (the last " + std::to_string(std::min<size_t>(g.formerCrew.size(), 10)) +
                                                                  " of " + std::to_string(g.formerCrew.size()) + ")") << "\n";
        size_t from = g.formerCrew.size() > 10 ? g.formerCrew.size() - 10 : 0;
        for (size_t i = g.formerCrew.size(); i-- > from;) {
            const FormerCrew& c = g.formerCrew[i];
            std::string who = c.name + " (" + ROLES[(int)c.role].name + " L" + std::to_string(c.level) +
                              (c.stars ? " " + std::string((size_t)std::min(c.stars, 5), '*') : "") + ")";
            out() << "  " << padRight(who, 26) << padRight(TRAITS[(int)c.trait].name, 14) << padRight(c.how, 16)
                  << paint(Style::Muted, "at " + playTime(c.at)) << "\n";
        }
    }

    out() << paint(Style::Bold, "Time") << "\n";
    row("Played", playTime(g.playSeconds));
    int ti = titleIndex(renownOf(g));
    for (int t = 1; t <= ti; ++t) row(std::string(TITLES[t].name), "at " + playTime(g.titleReachedAt[t]));

    std::vector<long long> cols = historyColumns(g, 60);
    if (cols.size() >= 2 && g.history.size() >= 2) {
        out() << "\n" << paint(Style::Bold, "Earned in total, over your play time") << paint(Style::Muted, "  (log scale)") << "\n";
        std::vector<std::string> chart = logChart(cols, 8);
        for (size_t r = 0; r < chart.size(); ++r) {
            std::string label = r == 0 ? compact(cols.back()) : r + 1 == chart.size() ? compact(std::max(1LL, cols.front())) : "";
            out() << padLeft(label, 7) << " " << paint(Style::Tokens, chart[r]) << "\n";
        }
        out() << padRight("        " + playTime(g.history.front().playSeconds), 60) << playTime(g.playSeconds) << "\n";
    } else {
        out() << paint(Style::Muted, "\nThe chart fills in as you play (a point every 5 minutes).") << "\n";
    }
}
