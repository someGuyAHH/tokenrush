#include "commands.hpp"

#include <algorithm>
#include <bit>
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
#include "core/game.hpp"
#include "core/market.hpp"
#include "core/save.hpp"
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
    return "[" + paint(Style::Bold, TITLES[titleIndex(renownOf(g))].name) + "] " + promptOf(g);
}

void printPending(Shared& s) {
    for (const LogEntry& e : s.log.drain()) printEntry(e);
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

    if (hasChoice(g))
        out() << "Offer:  " << paint(Style::Event, choiceText(g)) << paint(Style::Muted, "  (yes/no, " + std::to_string(g.choice.secondsLeft) + "s)") << "\n";
    if (g.race.stake > 0)
        out() << "Race:   " << (g.manualMines - g.race.startMines) << "/" << g.race.target << " mines, "
              << g.race.secondsLeft << "s left (" << num(g.race.stake) << " tokens on it)\n";
    if (g.investment.amount > 0)
        out() << "Loan:   " << num(g.investment.amount) << " tokens with the Investor, due back in " << duration(g.investment.secondsLeft) << "\n";

    if (isUnlocked(g, Feature::Contracts)) {
        out() << "Heat:   " << paint(Style::Heat, heatBar(g.heat, MAX_HEAT)) << " " << g.heat << "/" << MAX_HEAT;
        if (g.heat >= AUDIT_MIN_HEAT) out() << paint(Style::Warn, "  (audits possible)");
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
              << " tok/s  next cost: " << num(ups[2].cost) << "\n";
    out() << "  Bot     lvl " << g.bot;
    if (g.bot > 0) out() << "  mines every " << botInterval(g.bot) << "s";
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
        if (isUnlocked(g, Feature::Crew)) row("Crew", num(g.crewHired) + " hired, mined " + num(g.crewMined) + " tokens");
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
