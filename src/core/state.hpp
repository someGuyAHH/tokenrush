#pragma once
// Game state, plus the objects that are shared between threads.

#include <array>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "content.hpp"

inline constexpr double SHARD_FAIR_PRICE = 150.0;   // where the shard price drifts back to

// ----- Messages -----
// How a message should look. The core only says what kind of news it is;
// the front end decides what that means (colors in the log pane and the Console).
enum class Sev { Info, Good, Bad, Warn, Event };

struct Note {
    Sev sev;
    std::string text;
};
using Notes = std::vector<Note>;   // what a player action returns instead of printing

// ----- Income sources -----
// Every token that enters the economy is booked under one of these (see earn() in game.hpp),
// so the Stats tab can say where the money comes from.
enum class Source : int { Mining, Drones, Bot, Crew, Contracts, Other, Count };
inline constexpr int SOURCE_COUNT = (int)Source::Count;
using SourceTotals = std::array<long long, SOURCE_COUNT>;

// ----- Crew and contracts -----
struct CrewMember {
    std::string name;
    Role role = Role::Miner;
    int level = 1;
    Trait trait = Trait::None;
    int stars = 0;               // promotions: each raises the level cap and how much a level is worth
    int raises = 0;              // raises they asked for and got (a choice event): +1 wage, +25% work each
    int sulk = 0;                // seconds of sulking left after a refused raise (half the work)
    bool missedPay = false;      // Loyal only: already stayed through one missed payday
    int away = 0;                // seconds until they're back from an expedition (0 = here)
    int trip = 0;                // which expedition (index into EXPEDITIONS, crew.hpp)
    int hurt = 0;                // seconds of half effort left after an expedition went badly
};

// ----- People who left, and what they left behind -----
struct Keepsake {                // on your shelf: from a crew member who quit, or got their third star
    std::string item;            // "Mara's Lucky Wrench"
    std::string from;            // "Mara"
    Role role = Role::Miner;
    std::string why;             // "quit when you couldn't pay"
    long long at = 0;            // play seconds
};
struct FormerCrew {              // the Stats tab remembers everyone who ever left
    std::string name;
    Role role = Role::Miner;
    int level = 1;
    int stars = 0;
    Trait trait = Trait::None;
    std::string how;             // "quit (unpaid)", "fired"
    long long at = 0;            // play seconds
};
inline constexpr size_t KEEPSAKE_MAX = 30;      // the oldest go first past these
inline constexpr size_t FORMER_CREW_MAX = 40;

// ----- Choice events -----
enum class ChoiceKind : int { None, ShadyBroker, Investor, Rival, Raise, Bribe, Dealer };

struct Choice {                  // the question on the table, if any (not saved: it would expire anyway)
    ChoiceKind kind = ChoiceKind::None;
    int secondsLeft = 0;
    long long tokens = 0;        // what it costs or stakes
    long long shards = 0;        // Shady Broker: how many shards
    std::string crewName;        // Raise: who's asking
    int curio = -1;              // Dealer: which curio
};

struct Investment {              // money lent to the Investor (saved)
    long long amount = 0;
    long long payback = 0;
    int secondsLeft = 0;
    bool scam = false;           // decided when you lend it; you find out at the end
};

struct Race {                    // the Rival Miner's bet (not saved: quitting mid-race loses it)
    long long stake = 0;
    long long startMines = 0;
    long long target = 0;
    int secondsLeft = 0;
};

// ----- Disasters (see disasters.hpp) -----
struct Disaster {                // the warning that's running, if any (not saved: quitting drops it)
    int kind = -1;               // a DisasterKind, or -1 for none
    int secondsLeft = 0;
    long long tokens = 0;        // what stopping it costs
    long long shards = 0;
    int target = -1;             // a Quake's Orrery stage, a Short Circuit's gadget
    bool chasing = false;        // Thieves: you're after them
    long long chaseStart = 0;    // manualMines when the chase began
};

// ----- History -----
struct HistorySample {           // one point on the Stats chart
    long long playSeconds = 0;
    long long lifetime = 0;
    long long tokens = 0;
    long long shards = 0;
};

struct Contract {
    ContractKind kind = ContractKind::Earn;
    int client = 0;              // index into CLIENTS
    bool big = false;            // big jobs take a shard stake and pay more
    bool signature = false;      // a Trusted client's special job: longer, no stake, double pay
    long long target = 0;
    long long start = 0;         // the stat's value when the contract was accepted
    int duration = 0;            // seconds you get once you accept
    int secondsLeft = 0;         // counts down only while the game is running
    long long stake = 0;         // shards you put up; lost if you fail
    long long rewardTokens = 0;
    long long rewardShards = 0;
    int expiresIn = 0;           // offers only: seconds until it leaves the board
    int tipLevel = -1;           // how it looks right now: a TipLevel (contracts.hpp); -1 = not judged yet (not saved)
    std::string tip;             // ...and why, in a sentence (not saved)
};

// ----- Tasks (tasks.hpp) -----
struct TaskProgram;              // a task's text, compiled (tasks.cpp)

struct Task {
    std::string text;            // what the player wrote: the task itself (the rest is bookkeeping)
    bool on = true;
    bool quiet = false;          // runs without a log line
    long long runs = 0;
    long long fees = 0;          // tokens paid in fees, all told
    long long lastRun = -1;      // playSeconds of the last run; -1 = never
    bool wasTrue = false;        // `when`: the condition last second
    bool pending = false;        // `when`: it became true, but the action couldn't happen yet
    long long nextAt = 0;        // `every`: playSeconds of the next check
    // Not saved:
    std::shared_ptr<const TaskProgram> prog;   // null until compiled (or when the text doesn't compile)
    std::string error;           // why the text doesn't compile ("" = it does)
    std::string waiting;         // why it didn't run the last time it wanted to ("" = it did, or didn't need to)
};

struct GameState {
    long long tokens = 0;
    long long shards = 0;

    int pickaxe = 0; // +base mining yield
    int magnet  = 0; // increases bonus/jackpot odds
    int drone   = 0; // passive income
    int bot     = 0; // auto-mining

    int streak = 0;              // increases manual mining multiplier
    double passiveCarry = 0.0;   // fractional passive tokens

    bool autoUnlocked = false;   // Auto-Buyer license owned
    bool autoOn = false;         // Auto-Buyer enabled

    int event = EV_NONE;
    std::time_t eventEnds = 0;

    // Market
    double shardPrice = SHARD_FAIR_PRICE;        // mid price in tokens; see market.cpp
    double lastReportedPrice = SHARD_FAIR_PRICE; // the log mentions big moves away from this
    std::deque<double> priceHistory;    // one sample per few seconds, for the chart (not saved)
    int historyClock = 0;

    // Heat: goes up when you fail contracts, makes the market worse, can trigger audits
    int heat = 0;
    int heatClock = 0;                  // seconds until heat cools by 1

    // Contracts
    std::vector<Contract> contracts;    // the jobs you're working on (1 slot, 2 with the Orrery's Second Ring)
    std::vector<Contract> offers;       // regenerated after loading (not saved)
    int offerClock = 0;                 // seconds until a fresh offer appears
    int tipClock = 0;                   // seconds until the tips are judged again (not saved)
    std::array<int, CLIENT_COUNT> standing{};   // with each client, -10..10 (see contracts.hpp)
    long long contractStreak = 0;       // streak counted only from when the contract started
    long long contractPeakStreak = 0;   // best contractStreak so far

    // How fast you've actually been earning and mining lately (moving averages, not saved).
    // Contracts size their targets from these, so they stay a stretch at every stage of the game.
    double incomeRate = 0.0;            // tokens per second
    double mineRate = 0.0;              // manual mines per second
    long long rateMarkEarned = -1;      // lifetimeEarned at the last tick (-1 = not started)
    long long rateMarkMines = 0;
    long long rateSkip = 0;             // contract payouts, which shouldn't count as "income"
    std::deque<long long> earnedSamples;   // lifetimeEarned once a second, newest last (not saved)
    SourceTotals earnedBy{};            // lifetime tokens per Source (saves from before v9 start at 0)
    std::deque<SourceTotals> sourceSamples;   // earnedBy once a second, like earnedSamples (not saved)

    // The market ledger: what trading shards has cost and paid (market orders and the Shady Broker)
    long long shardsBought = 0;
    long long shardsSold = 0;
    long long tokensOnBuys = 0;         // tokens spent buying shards
    long long tokensFromSells = 0;      // tokens received selling them
    long long tradeCost = 0;            // paid to the spread and price impact, against the mid price

    // Crew
    std::vector<CrewMember> crew;
    std::vector<CrewMember> candidates; // who you can hire right now (not saved)
    int candidateClock = 0;             // seconds until the candidates change
    int paydayClock = 0;                // seconds until the crew wants their shards
    int crewClock = 0;                  // seconds since the crew started working (not saved)
    long long crewMinedSinceReport = 0;

    // Stats (these feed the goals)
    long long botTotal = 0;      // lifetime tokens mined by the bot
    long long lifetimeEarned = 0;
    long long manualMines = 0;
    long long jackpots = 0;
    long long bestStreak = 0;
    long long eventsSeen = 0;
    long long playSeconds = 0;
    long long shardsTraded = 0;
    long long contractsDone = 0;
    long long contractsFailed = 0;
    long long crewHired = 0;
    long long expeditionsDone = 0;
    long long crewMined = 0;
    long long audits = 0;

    // Progress and purchases
    // Choice events
    Choice choice;
    int choiceClock = 0;             // seconds until the next one (0 = not set yet; not saved)
    Investment investment;
    Race race;

    // Disasters and the damage they leave
    Disaster disaster;
    int disasterClock = 0;           // seconds of play until the next one (0 = not set yet)
    int dronesDown = 0;              // Drones burnt out by a Power Surge...
    int dronesDownSecs = 0;          // ...until this runs out
    int machinesPaused = 0;          // seconds the Drones and Bot stay off (breakers pulled)
    bool botBroken = false;          // until repaired
    std::uint32_t orreryCracked = 0; // bit i = ORRERY[i] cracked: its perk is off until repaired
    std::uint32_t gadgetsBroken = 0; // bit i = GADGETS[i] broken until repaired
    int gadgetsUnplugged = 0;        // seconds every gadget stays off (unplugged against a Short Circuit)
    long long disastersHit = 0;
    long long disastersStopped = 0;

    // The Constellations (stars.hpp): what's lit, and the feats being tracked
    std::uint32_t starsLit = 0;      // bit i = CONSTELLATIONS[i] lit
    std::uint32_t starsFound = 0;    // done by an action, lit by the next checkGoals (not saved)
    long long lastContractAt = -1'000'000;   // play seconds when the last contract was finished (the Twins)
    int fullCrewPaydays = 0;         // paydays in a row with a full crew of 5 and nobody leaving (the Hearth)
    int stoppedInRow = 0;            // disasters stopped since the last one hit (the Lantern)
    std::uint32_t signatureClients = 0;   // bit i = finished a signature job for CLIENTS[i] (the Crown)
    long long heat10At = -1;         // play seconds when heat was last 10 (the Phoenix)
    long long handsOffSince = 0;     // the Hourglass's hour: when it started...
    long long handsOffFrom = 0;      // ...lifetimeEarned then...
    long long handsOffMines = 0;     // ...and manualMines then

    // Infection (see content.hpp): a strain, until the Orrery cures it
    Strain strain = Strain::None;
    bool cured = false;

    // Stats and history (the Stats tab)
    long long biggestJackpot = 0;
    long long bestEarnRate = 0;      // tokens/s, over EARN_WINDOW_SECONDS
    long long titleReachedAt[TITLE_COUNT] = {};   // play seconds when each title was reached; -1 = before records
    std::vector<HistorySample> history;

    std::uint32_t goalsDone = 0;     // bit i = GOALS[i] complete
    std::uint32_t secretsDone = 0;   // bit i = SECRETS[i] found (and paid)
    std::uint32_t secretsFound = 0;  // spotted by an action or a tick, paid by the next checkGoals (not saved)
    int refusedInRow = 0;            // offers you said no to since the last yes (a secret goal)
    std::vector<Keepsake> keepsakes;
    std::vector<FormerCrew> formerCrew;
    std::uint32_t unlocked = 0;      // bit i = FEATURES[i] is open (see game.hpp, Unlocks)
    std::uint32_t curiosOwned = 0;   // bit i = CURIOS[i] owned
    std::uint32_t promptsOwned = 1;  // bit i = PROMPTS[i] owned; bit 0 (the default) is free
    int promptSel = 0;               // index into PROMPTS, or PROMPT_COUNT for the custom prompt
    bool customPromptOwned = false;
    std::string customPrompt;        // 1-5 characters, shown with a space after it
    int themeSel = 0;                // index into THEMES
    std::uint32_t gadgetsOwned = 0;  // bit i = GADGETS[i] built
    std::array<int, GADGET_SLOTS> gadgetSlots{-1, -1, -1};   // what's equipped (indexes into GADGETS, -1 = empty)
    std::uint32_t themesOwned = 0;   // bit i = THEMES[i] bought (free ones count as owned anyway)
    bool customThemeOwned = false;   // the custom theme (themeSel CUSTOM_THEME), bought once
    Palette customTheme{};           // its colors, by Hue
    int monolithStage = 0;           // stages completed
    long long monolithProgress = 0;  // tokens poured into the current stage
    int orreryStage = 0;             // stages completed
    long long orreryProgress = 0;    // shards put into the current stage

    // Tasks: 0 slots until the Automation License is bought
    int taskSlots = 0;
    std::vector<Task> tasks;
    std::array<long long, 60> taskFeeWindow{};   // fees paid in each of the last 60 seconds, by playSeconds % 60 (not saved)

    std::time_t lastUpdate = 0;  // for passive
    std::time_t lastMine = 0;    // for streak timeout
};

// ----- The message log -----
// Background threads never print; they push here. The TUI draws recent() in its pinned log pane,
// and the Console's 'log' command shows more of it.
struct LogEntry {
    std::string stamp;   // "14:03:22"
    Sev sev;
    std::string text;
};

class MessageLog {
public:
    void push(Sev sev, const std::string& text);
    void push(const Note& n) { push(n.sev, n.text); }

    std::vector<LogEntry> recent(size_t n);      // the last n (the log keeps HISTORY_MAX)

private:
    static constexpr size_t HISTORY_MAX = 400;
    std::mutex m_;
    std::deque<LogEntry> history_;
};

struct Shared {
    std::mutex m;        // guards g
    GameState g;
    std::mutex saveMtx;  // serializes file writes/removal. Lock order: saveMtx -> m, never the other way.
    std::atomic<long long> lastSave{0};
    MessageLog log;
};
