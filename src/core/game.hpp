#pragma once
// Game rules: tunables, economy, passive income, goals and titles.
// Nothing in here prints or touches files. Functions that take a GameState& expect the
// caller to hold Shared::m (or to own the state outright, like the simulator).

#include <array>
#include <ctime>
#include <string>
#include <vector>

#include "content.hpp"
#include "state.hpp"

// ----- Tunables -----
inline constexpr long long PASSIVE_CAP_SECONDS  = 12LL * 60 * 60; // 12 hours
inline constexpr long long STREAK_TIMEOUT       = 15;             // seconds (6 with the Amnesia strain: see streakTimeout)
inline constexpr int       AUTOSAVE_SECONDS     = 30;
inline constexpr int       BOT_REPORT_SECONDS   = 30;
inline constexpr int       AUTOBUY_SECONDS      = 5;
inline constexpr long long AUTOBUY_RESERVE      = 3;              // buy once tokens >= 3x cost
inline constexpr long long AUTOBUY_LICENSE_COST = 2000;
// (autoBuyOnce, below: the Auto-Buyer's one purchase)
inline constexpr double    EVENT_CHANCE_PER_SEC = 1.0 / 240.0;    // ~one event per 4 minutes
inline constexpr int       MAX_LEVEL            = 1'000'000;      // sanity cap for upgrade levels
// Upgrade prices are base * (level+1)^2 up to a knee, then steeper: each level past the knee adds
// 1/slope to the price's multiplier, so the price tends to level^3. With Resonance, income is
// Pickaxe * Magnet * (Drones + Bot) while each price grows only with its level squared, which pays
// every upgrade back in the same time at any level: income doubled on a fixed clock. The knees sit
// above what two hours of play reaches (Pickaxe 270, Drone 123), so the early game is unchanged.
inline constexpr int       COST_KNEE_YIELD      = 300;            // Pickaxe and Magnet
inline constexpr int       COST_SLOPE_YIELD     = 300;
inline constexpr int       COST_KNEE_AUTO       = 150;            // Drone and Mining Bot
inline constexpr int       COST_SLOPE_AUTO      = 300;

// ----- Infection -----
bool infected(const GameState& g, Strain s);   // has this strain and isn't cured yet
long long streakTimeout(const GameState& g);    // STREAK_TIMEOUT, less with Amnesia or the Tuning Fork
double bonusChance(const GameState& g, double odds = 1.0);     // a mine turning into a bonus (odds: Gold Rush)
double jackpotChance(const GameState& g, double odds = 1.0);   // ...or a jackpot
double bonusSize(const GameState& g);                          // how many base yields a bonus adds
double jackpotSize(const GameState& g);                        // ...and a jackpot
double bonusBoost(const GameState& g, double odds);            // Gold Rush past the odds cap: extra pay instead
double jackpotBoost(const GameState& g, double odds);
double humBoost(const GameState& g);                           // the Tuning Fork's boost to Drones and the Bot (1 = none)
double handMult(const GameState& g);                           // gadgets that change what mining by hand pays

// ----- Upgrades -----
long long costPickaxe(int lvl);
long long costMagnet(int lvl);
long long costDrone(int lvl);
long long costBot(int lvl);

int baseYield(const GameState& g);

// Milestones: Drones and the Mining Bot double everything they make at 100, 1,000, 10,000...
// levels (each power of ten from 100). A level adds a fixed amount while its price keeps growing,
// so without these a late Drone or Bot level took hours to pay for itself. (The Pickaxe and the
// Magnet don't need them: they raise the average mine everything else is built on, so their value
// keeps up on its own, and milestones there would multiply each other through it. More steps per
// power of ten, as there used to be, made income double faster than the prices could follow.)
int milestonesAt(int level);           // how many it has passed
double milestoneMult(int level);       // 2 to that
int nextMilestone(int level);          // the next one
int botInterval(int botLvl);              // seconds between Mining Bot mines, by level
int botIntervalOf(const GameState& g);    // the same, with gadgets (Night Shift halves it)

struct UpgradeRef { const char* name; long long cost; int* lvl; };
std::array<UpgradeRef, 4> upgrades(GameState& g);

// ----- Gadgets -----
bool gadgetOn(const GameState& g, Gadget x);         // built, equipped and working (not broken or unplugged)
bool gadgetEquipped(const GameState& g, Gadget x);   // in a slot, working or not
long long passiveCapSeconds(const GameState& g);   // how long Drones keep earning while you're away

// ----- Events -----
int activeEvent(const GameState& g, std::time_t t);   // EV_NONE once it has expired
EventFx fxNow(const GameState& g, std::time_t t);

// ----- Orrery perks -----
inline constexpr int MAX_CREW_SLOTS = 5;    // the most crew slots there can ever be
inline constexpr int CREW_LEVEL_LIMIT = 8;  // the highest crew level there can ever be
bool stageWorks(const GameState& g, int stage);   // built and not cracked by a Quake
int crewCap(const GameState& g);            // 4, or 5 after the Base Plate
int crewLevelCap(const GameState& g);       // 5, or 8 after the Gearwork
int contractSlots(const GameState& g);      // 1, or 2 after the Second Ring
bool hasResonance(const GameState& g);      // after the First Ring
// A funded Orrery stage takes time to assemble (play time: only while the game is open) before
// its perk works and the next stage can be funded: 1 minute for the first, twice as long each stage after.
inline constexpr int ORRERY_BUILD_FIRST  = 60;
inline constexpr int ORRERY_BUILD_GROWTH = 2;
int orreryBuildSeconds(int stage);          // 60, 120, 240, 480, 960

// ----- Income -----
// Every token that enters the economy goes through earn(), so lifetime stats stay honest,
// booked under the Source it came from.
void earn(GameState& g, long long amount, Source src);
// A lump sum (a contract payout, a won race, the Investor's profit): earned like any other, but it
// isn't a sign you got faster (incomeRate leaves it out) and it isn't the work a running Earn job
// asked for (their start moves along with it).
void earnWindfall(GameState& g, long long amount, Source src);
const char* sourceName(Source src);            // "Mining by hand", "Drones", ...
// Resonance (Orrery stage 2): each Drone level also earns this share of an expected manual mine
// per second, and the Mining Bot swings like a manual miner at a fraction of the power.
inline constexpr double RESONANCE_DRONE   = 0.003;    // per Drone level, per second
inline constexpr double RESONANCE_BOT     = 0.10;    // bot swing at level 0...
inline constexpr double RESONANCE_BOT_LVL = 0.005;    // ...plus this per Bot level
double resonantBotPower(const GameState& g);         // share of an expected mine per bot swing

double passiveRatePerSec(const GameState& g);
double botRatePerSec(const GameState& g);      // average, events ignored (0 while it's broken or the breakers are pulled)
double botRateIfRunning(const GameState& g);   // the same, as if it were running (for sizing jobs)
double expectedMine(const GameState& g);       // average manual mine, streak and events ignored
double incomeEstimate(const GameState& g);     // tokens/s from the formulas, for one mine every two seconds
void updateRates(GameState& g);                // once per second: refresh incomeRate and mineRate
// Tokens earned per second over the last few seconds, from everything (mining, bot, drones,
// crew, contract pay). Measured from lifetimeEarned, so spending doesn't drag it down.
inline constexpr int EARN_WINDOW_SECONDS = 10;
double recentEarnRate(const GameState& g);
// What you'd make a second from the formulas, not from what came in: Drones, the Bot and crew Miners
// as they are now, plus mining by hand at the last EST_MINE_WINDOW seconds' pace and your streak.
// Events count; luck, contracts and offers don't.
inline constexpr int EST_MINE_WINDOW = 5;
double estimatedIncome(const GameState& g, std::time_t t);
std::array<double, SOURCE_COUNT> recentSourceRates(const GameState& g);   // the same, split by Source
std::string incomeSplit(const GameState& g);   // "mining 61%, drones 27%, bot 12%" (empty while nothing comes in)
void updatePassive(GameState& g, std::time_t t);
void applyOfflineEarnings(GameState& g, std::time_t tNow);
void normalizeStreak(GameState& g, std::time_t t);
// The Auto-Buyer's purchase, every AUTOBUY_SECONDS: the cheapest upgrade, if you have AUTOBUY_RESERVE
// times its cost. The name bought (and what it cost), or nullptr.
const char* autoBuyOnce(GameState& g, long long& cost);
void resetGame(GameState& g, std::time_t t);   // 'wipe': everything back to the start, except the infection and aliases

// Console aliases (the commands live in shell/aliases.cpp; the save checks names with this too)
inline constexpr int ALIAS_MAX = 50;          // per profile
inline constexpr size_t ALIAS_NAME_MAX = 16;
inline constexpr size_t ALIAS_TEXT_MAX = 120; // what it runs
bool aliasNameOk(const std::string& name);   // 1-16 of a-z 0-9 - _ (lowercase)

// ----- Goals, renown, titles -----
long long statValue(const GameState& g, Stat st);
int renownOf(const GameState& g);
int titleIndex(int renown);
void checkGoals(GameState& g, MessageLog& log);   // also records title times, cures an infection and pays secret goals
void spotSecret(GameState& g, Secret s);          // an action saw a secret goal happen; checkGoals pays it

// ----- Stats and history -----
inline constexpr int STATS_EVERY = 300;   // play seconds between chart samples
inline constexpr size_t STATS_HISTORY_MAX = 240;  // past this, every other old sample is dropped
void recordStats(GameState& g);             // once per second (worldTick)
// The chart: lifetimeEarned at `columns` evenly spaced moments from 0 to now (play time).
std::vector<long long> historyColumns(const GameState& g, int columns);

// ----- Unlocks -----
// Features open as titles are earned (FEATURES in content.cpp says which title opens what).
// Once open, a feature stays open.
bool isUnlocked(const GameState& g, Feature f);
std::string lockedText(const GameState& g, Feature f);  // "The Market opens at the title Baron (18 renown; you have 12)."
int nextUnlock(const GameState& g);                      // index into FEATURES of the next one to open, or -1
Notes syncUnlocks(GameState& g);                         // open whatever the current title allows; one note per feature opened
void grandfatherUnlocks(GameState& g);
int featureOfStat(Stat st);                              // the Feature a goal's stat needs, or -1                   // old saves: keep open whatever was already in use
