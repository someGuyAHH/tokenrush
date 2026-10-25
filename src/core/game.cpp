#include "game.hpp"

#include <algorithm>
#include <cctype>
#include <bit>
#include <cmath>
#include <limits>
#include <iterator>
#include <string>

#include "contracts.hpp"
#include "crew.hpp"
#include "stars.hpp"
#include "util.hpp"

// ----- Upgrades -----
// base * n^2, times 1 + (n - knee)/slope once n passes the knee (see COST_KNEE_YIELD). Exact below it.
static long long levelCost(long long base, int lvl, int knee, int slope) {
    double n = (double)lvl + 1.0;
    double cost = (double)base * n * n * (1.0 + std::max(0.0, n - knee) / slope);
    return cost >= (double)TOKEN_CAP ? TOKEN_CAP : (long long)cost;
}
long long costPickaxe(int lvl) { return levelCost(25,  lvl, COST_KNEE_YIELD, COST_SLOPE_YIELD); }
long long costMagnet (int lvl) { return levelCost(60,  lvl, COST_KNEE_YIELD, COST_SLOPE_YIELD); }
long long costDrone  (int lvl) { return levelCost(120, lvl, COST_KNEE_AUTO,  COST_SLOPE_AUTO); }
long long costBot    (int lvl) { return levelCost(400, lvl, COST_KNEE_AUTO,  COST_SLOPE_AUTO); }

int baseYield(const GameState& g) { return 1 + g.pickaxe; }

int nextMilestone(int level) {
    long long step = 100;
    while (step <= level) step *= 10;
    return (int)std::min<long long>(step, std::numeric_limits<int>::max());
}

int milestonesAt(int level) {
    int n = 0;
    for (long long step = 100; step <= level; step *= 10) ++n;
    return n;
}

double milestoneMult(int level) { return std::ldexp(1.0, milestonesAt(level)); }
int botInterval(int botLvl) { return std::max(2, 10 - botLvl); }
int botIntervalOf(const GameState& g) {
    int i = botInterval(g.bot);
    return gadgetOn(g, Gadget::NightShift) ? std::max(1, i / 2) : i;
}

// ----- Gadgets -----
bool gadgetEquipped(const GameState& g, Gadget x) {
    return hasBit(g.gadgetsOwned, (int)x) && std::find(g.gadgetSlots.begin(), g.gadgetSlots.end(), (int)x) != g.gadgetSlots.end();
}

int gadgetWarmupLeft(const GameState& g, Gadget x) {
    for (int i = 0; i < GADGET_SLOTS; ++i)
        if (g.gadgetSlots[(size_t)i] == (int)x) return std::max(0, g.gadgetWarmup[(size_t)i]);
    return 0;
}

bool gadgetOn(const GameState& g, Gadget x) {
    return gadgetEquipped(g, x) && !hasBit(g.gadgetsBroken, (int)x) && g.gadgetsUnplugged <= 0 && gadgetWarmupLeft(g, x) <= 0;
}

long long passiveCapSeconds(const GameState& g) { return gadgetOn(g, Gadget::LavaCore) ? 4LL * 3600 : PASSIVE_CAP_SECONDS; }

// ----- Mining odds -----
double bonusChance(const GameState& g, double odds) {
    double c = (0.06 + 0.04 * g.magnet) * odds;
    return std::min(0.70, c);
}
double jackpotChance(const GameState& g, double odds) {
    return std::min(0.20, (0.01 + 0.005 * g.magnet) * odds);
}
// The odds stop at 70% and 20%, but the Magnet keeps making both pay more, level after level.
double bonusSize(const GameState& g) { return (8.0 + 4.0 * g.magnet) * (gadgetOn(g, Gadget::LuckyPenny) ? 0.9 : 1.0); }
double jackpotSize(const GameState& g) { return 80.0 + 20.0 * g.magnet; }
// Gold Rush doubles the odds; whatever the cap swallows comes back as pay, so it's still twice the
// bonuses and jackpots once the odds are maxed.
double bonusBoost(const GameState& g, double odds) { return odds * bonusChance(g) / std::max(1e-9, bonusChance(g, odds)); }
double jackpotBoost(const GameState& g, double odds) { return odds * jackpotChance(g) / std::max(1e-9, jackpotChance(g, odds)); }
// The Tuning Fork: a streak of 20 or more sets the machines humming.
double humBoost(const GameState& g) { return gadgetOn(g, Gadget::TuningFork) && g.streak >= 20 ? 1.3 : 1.0; }
double handMult(const GameState& g) { return gadgetOn(g, Gadget::NightShift) ? 0.75 : 1.0; }

std::array<UpgradeRef, 4> upgrades(GameState& g) {
    long long m = infected(g, Strain::Rust) ? 2 : 1;
    return {{
        {"Pickaxe",    m * costPickaxe(g.pickaxe), &g.pickaxe},
        {"Magnet",     m * costMagnet(g.magnet),   &g.magnet},
        {"Drone",      m * costDrone(g.drone),     &g.drone},
        {"Mining Bot", m * costBot(g.bot),         &g.bot},
    }};
}

// ----- Infection -----
bool infected(const GameState& g, Strain s) { return g.strain == s && !g.cured; }
long long streakTimeout(const GameState& g) {
    long long t = infected(g, Strain::Amnesia) ? 6 : STREAK_TIMEOUT;
    return gadgetOn(g, Gadget::TuningFork) ? std::min(t, 8LL) : t;   // the Tuning Fork wants you mining
}

// ----- Events -----
int activeEvent(const GameState& g, std::time_t t) {
    return (g.event != EV_NONE && t < g.eventEnds) ? g.event : EV_NONE;
}

EventFx fxNow(const GameState& g, std::time_t t) { return eventFx(activeEvent(g, t)); }

// ----- Orrery perks -----
bool stageWorks(const GameState& g, int stage) { return g.orreryStage > stage && !hasBit(g.orreryCracked, stage); }
int crewCap(const GameState& g)       { return stageWorks(g, 0) ? 5 : 4; }
bool hasResonance(const GameState& g) { return stageWorks(g, 1); }
int orreryBuildSeconds(int stage) {
    int s = ORRERY_BUILD_FIRST;
    for (int i = 0; i < stage; ++i) s *= ORRERY_BUILD_GROWTH;
    return s;
}
int contractSlots(const GameState& g) { return stageWorks(g, 2) ? 2 : 1; }
int crewLevelCap(const GameState& g)  { return stageWorks(g, 3) ? 8 : 5; }

// ----- Income -----
void earn(GameState& g, long long amount, Source src) {
    if (amount <= 0) return;
    g.tokens = addTokens(g.tokens, amount);
    g.lifetimeEarned = addTokens(g.lifetimeEarned, amount);
    long long& by = g.earnedBy[(size_t)std::clamp((int)src, 0, SOURCE_COUNT - 1)];
    by = addTokens(by, amount);
}

const char* sourceName(Source src) {
    switch (src) {
        case Source::Mining:    return "Mining by hand";
        case Source::Drones:    return "Drones";
        case Source::Bot:       return "Mining Bot";
        case Source::Crew:      return "Crew";
        case Source::Contracts: return "Contracts";
        case Source::Other:     return "Offers and other";
        case Source::Count:     break;
    }
    return "?";
}

double resonantBotPower(const GameState& g) { return RESONANCE_BOT + RESONANCE_BOT_LVL * g.bot; }

double passiveRatePerSec(const GameState& g) {
    double perDrone = 0.5;
    if (hasResonance(g)) perDrone += RESONANCE_DRONE * expectedMine(g);
    if (g.machinesPaused > 0) return 0.0;                       // breakers pulled
    int working = std::max(0, g.drone - g.dronesDown);           // some may be burnt out by a surge
    return perDrone * (double)working * milestoneMult(g.drone) * (gadgetOn(g, Gadget::LavaCore) ? 1.5 : 1.0) * humBoost(g);
}

double botRatePerSec(const GameState& g) {
    if (g.botBroken || g.machinesPaused > 0) return 0.0;
    return botRateIfRunning(g);
}

double botRateIfRunning(const GameState& g) {
    if (g.bot <= 0) return 0.0;
    double m = milestoneMult(g.bot);
    if (hasResonance(g)) return expectedMine(g) * resonantBotPower(g) / botIntervalOf(g) * humBoost(g) * m;
    double perMine = baseYield(g) * (0.6 + 0.2 * g.bot);
    double bonus = std::min(0.35, 0.03 + 0.02 * g.magnet) * baseYield(g) * (4.0 + 2.0 * g.magnet);
    return (perMine + bonus) / botIntervalOf(g) * humBoost(g) * m;
}

double expectedMine(const GameState& g) {
    double base = baseYield(g);
    return base * 1.5 + bonusChance(g) * base * bonusSize(g) + jackpotChance(g) * base * jackpotSize(g);
}

double incomeEstimate(const GameState& g) {
    double manual = expectedMine(g) * 1.5 * 0.5;   // medium streak, one mine every two seconds
    double crew = 0.0;
    for (const CrewMember& c : g.crew)
        if (c.role == Role::Miner && c.away == 0) crew += expectedMine(g) * (0.15 + 0.10 * c.level) / 5.0;
    return passiveRatePerSec(g) + botRatePerSec(g) + manual + crew;
}

void updateRates(GameState& g) {
    if (g.rateMarkEarned < 0) {   // first tick after starting or loading
        g.rateMarkEarned = g.lifetimeEarned;
        g.rateMarkMines = g.manualMines;
        g.rateSkip = 0;
        return;
    }
    double earned = (double)(g.lifetimeEarned - g.rateMarkEarned - g.rateSkip);
    double mined = (double)(g.manualMines - g.rateMarkMines);
    g.rateMarkEarned = g.lifetimeEarned;
    g.rateMarkMines = g.manualMines;
    g.rateSkip = 0;
    g.incomeRate += (std::max(0.0, earned) - g.incomeRate) / 300.0;   // ~5 minute memory
    g.mineRate += (mined - g.mineRate) / 120.0;                        // ~2 minute memory

    g.earnedSamples.push_back(g.lifetimeEarned);
    while (g.earnedSamples.size() > (size_t)EARN_WINDOW_SECONDS + 1) g.earnedSamples.pop_front();
    g.mineSamples.push_back(g.manualMines);
    while (g.mineSamples.size() > (size_t)EST_MINE_WINDOW + 1) g.mineSamples.pop_front();
    g.sourceSamples.push_back(g.earnedBy);
    while (g.sourceSamples.size() > (size_t)EARN_WINDOW_SECONDS + 1) g.sourceSamples.pop_front();
}

double estimatedIncome(const GameState& g, std::time_t t) {
    EventFx fx = fxNow(g, t);
    double drones = passiveRatePerSec(g) * fx.passive;
    double bot = botRatePerSec(g) * fx.mining;
    double crew = 0.0;
    for (const CrewMember& c : g.crew)
        if (c.role == Role::Miner && !isAway(c)) crew += (double)minerSwing(g, c) * fx.mining / MINER_EVERY;

    // By hand: the last few seconds' mines, each at what a mine pays on average with the streak you have now.
    double hand = 0.0;
    if (g.mineSamples.size() >= 2) {
        double perSec = (double)(g.mineSamples.back() - g.mineSamples.front()) / (double)(g.mineSamples.size() - 1);
        bool streakAlive = g.lastMine != 0 && std::difftime(t, g.lastMine) < (double)streakTimeout(g);
        double mult = 1.0 + 0.05 * (double)std::min(streakAlive ? g.streak : 0, 20);   // as mine() does
        double base = baseYield(g);
        double perMine = base * 1.5 * mult + bonusChance(g, fx.odds) * base * bonusSize(g) * bonusBoost(g, fx.odds) +
                         jackpotChance(g, fx.odds) * base * jackpotSize(g) * jackpotBoost(g, fx.odds);
        hand = perSec * perMine * fx.mining * handMult(g);
    }
    return drones + bot + crew + hand;
}

double recentEarnRate(const GameState& g) {
    if (g.earnedSamples.size() < 2) return 0.0;
    double gained = (double)(g.earnedSamples.back() - g.earnedSamples.front());
    return std::max(0.0, gained) / (double)(g.earnedSamples.size() - 1);
}

// "mining 61%, drones 27%, bot 12%": where the last few seconds' income came from (biggest first).
std::string incomeSplit(const GameState& g) {
    auto rates = recentSourceRates(g);
    double total = 0.0;
    for (double r : rates) total += r;
    if (total <= 0.0) return "";
    std::vector<int> order;
    for (int i = 0; i < SOURCE_COUNT; ++i) if (rates[(size_t)i] / total >= 0.01) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return rates[(size_t)a] > rates[(size_t)b]; });
    static const char* const shortNames[SOURCE_COUNT] = {"mining", "drones", "bot", "crew", "contracts", "other"};
    std::string out;
    for (int i : order) out += (out.empty() ? "" : ", ") + std::string(shortNames[i]) + " " + percent(rates[(size_t)i] / total, 0);
    return out;
}

std::array<double, SOURCE_COUNT> recentSourceRates(const GameState& g) {
    std::array<double, SOURCE_COUNT> out{};
    if (g.sourceSamples.size() < 2) return out;
    const SourceTotals& a = g.sourceSamples.front();
    const SourceTotals& b = g.sourceSamples.back();
    for (int i = 0; i < SOURCE_COUNT; ++i)
        out[(size_t)i] = std::max(0.0, (double)(b[(size_t)i] - a[(size_t)i])) / (double)(g.sourceSamples.size() - 1);
    return out;
}

static void addPassiveForElapsed(GameState& g, long long elapsed, double mult) {
    if (elapsed <= 0) return;
    if (elapsed > passiveCapSeconds(g)) elapsed = passiveCapSeconds(g);

    double rate = passiveRatePerSec(g) * mult;
    if (rate <= 0.0) return;

    double total = g.passiveCarry + rate * (double)elapsed;
    long long add = toTokens(std::floor(total));
    g.passiveCarry = total - (double)add;
    earn(g, add, Source::Drones);
}

void updatePassive(GameState& g, std::time_t t) {
    if (g.lastUpdate == 0) { g.lastUpdate = t; return; }

    long long elapsed = (long long)std::difftime(t, g.lastUpdate);
    if (elapsed <= 0) { g.lastUpdate = t; return; } // clock went backwards or no time passed

    addPassiveForElapsed(g, elapsed, fxNow(g, t).passive);
    g.lastUpdate = t;
}

void applyOfflineEarnings(GameState& g, std::time_t tNow) {
    if (g.lastUpdate == 0) { g.lastUpdate = tNow; return; }
    long long elapsed = (long long)std::difftime(tNow, g.lastUpdate);
    if (elapsed <= 0) { g.lastUpdate = tNow; return; }
    addPassiveForElapsed(g, elapsed, 1.0); // events and the bot don't run offline
    if (elapsed >= 8 * 3600) spotSecret(g, Secret::WellRested);
}

void resetGame(GameState& g, std::time_t t) {
    Strain strain = g.strain;   // the infection belongs to the profile, not the run
    auto aliases = std::move(g.aliases);   // and Console aliases (and what's silenced) are settings, not progress
    std::uint32_t silenced = g.silenced;
    g = GameState{};
    g.strain = strain;
    g.aliases = std::move(aliases);
    g.silenced = silenced;
    g.lastUpdate = t;
}

const char* autoBuyOnce(GameState& g, long long& cost) {
    auto ups = upgrades(g);
    auto cheapest = std::min_element(ups.begin(), ups.end(), [](const UpgradeRef& a, const UpgradeRef& b) { return a.cost < b.cost; });
    if (g.tokens < cheapest->cost * AUTOBUY_RESERVE) return nullptr;
    cost = cheapest->cost;
    g.tokens -= cost;
    *cheapest->lvl += 1;
    return cheapest->name;
}

bool aliasNameOk(const std::string& name) {
    if (name.empty() || name.size() > ALIAS_NAME_MAX) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

void normalizeStreak(GameState& g, std::time_t t) {
    if (g.lastMine != 0 && std::difftime(t, g.lastMine) >= (double)streakTimeout(g)) {
        g.streak = 0;
    }
}

// ----- Goals, renown, titles -----
long long statValue(const GameState& g, Stat st) {
    switch (st) {
        case Stat::Lifetime:      return g.lifetimeEarned;
        case Stat::ManualMines:   return g.manualMines;
        case Stat::Jackpots:      return g.jackpots;
        case Stat::BestStreak:    return g.bestStreak;
        case Stat::BotTotal:      return g.botTotal;
        case Stat::EventsSeen:    return g.eventsSeen;
        case Stat::CuriosOwned:   return std::popcount(g.curiosOwned);
        case Stat::MonolithStage: return g.monolithStage;
        case Stat::UpgradeLevels: return (long long)g.pickaxe + g.magnet + g.drone + g.bot;
        case Stat::PlayMinutes:   return g.playSeconds / 60;
        case Stat::ShardsTraded:  return g.shardsTraded;
        case Stat::ContractsDone: return g.contractsDone;
        case Stat::CrewHired:     return g.crewHired;
        case Stat::CrewMined:     return g.crewMined;
        case Stat::OrreryStage:   return g.orreryStage;
        case Stat::CrewSize:      return (long long)g.crew.size();
        case Stat::BestStanding:  return bestStanding(g);
        case Stat::ShopCurios:    return std::popcount(g.curiosOwned & ((1u << SHOP_CURIO_COUNT) - 1));
        case Stat::GadgetsBuilt:  return std::popcount(g.gadgetsOwned);
        case Stat::Expeditions:   return g.expeditionsDone;
    }
    return 0; // unreachable, keeps the compiler happy
}

int renownOf(const GameState& g) {
    int r = 0;
    for (int i = 0; i < GOAL_COUNT; ++i)
        if (hasBit(g.goalsDone, i)) r += GOALS[i].renown;
    return r;
}

int titleIndex(int renown) {
    int idx = 0;
    for (int i = 0; i < TITLE_COUNT; ++i)
        if (renown >= TITLES[i].renown) idx = i;
    return idx;
}

void spotSecret(GameState& g, Secret s) { g.secretsFound |= 1u << (int)s; }

// The secret goals that are about how things stand, rather than something that just happened.
static void spotStandingSecrets(GameState& g) {
    auto spot = [&](bool cond, Secret s) { if (cond) spotSecret(g, s); };
    spot(g.refusedInRow >= 10, Secret::ColdFeet);
    spot(g.shards >= 10'000, Secret::Hoard);
    spot(g.bestStreak >= 500, Secret::Overkill);
    spot(g.contractsDone >= 10 && g.contractsFailed == 0, Secret::CleanHands);
    spot(g.titleReachedAt[3] > 0 && g.titleReachedAt[3] <= 25 * 60, Secret::Speedrun);   // 3 = Baron
    spot(g.tokens == 0 && g.lifetimeEarned >= 10'000, Secret::DownToZero);
    std::uint32_t dealerSet = 0;
    for (int i = 0; i < CURIO_COUNT; ++i) if (CURIOS[i].set == DEALER_SET) dealerSet |= 1u << i;
    spot((g.curiosOwned & dealerSet) == dealerSet, Secret::UnderTheCounter);
}

static void paySecrets(GameState& g, MessageLog& log) {
    spotStandingSecrets(g);
    std::uint32_t fresh = g.secretsFound & ~g.secretsDone;
    g.secretsFound = 0;
    for (int i = 0; i < SECRET_COUNT; ++i) {
        if (!hasBit(fresh, i)) continue;
        g.secretsDone |= 1u << i;
        g.shards += SECRETS[i].shards;
        log.push(Sev::Event, std::string("SECRET GOAL: ") + SECRETS[i].name + "! " + SECRETS[i].desc + ". (+" +
                             std::to_string(SECRETS[i].shards) + " shards)");
    }
}

void checkGoals(GameState& g, MessageLog& log) {
    paySecrets(g, log);
    starsCheck(g, log);
    int titleBefore = titleIndex(renownOf(g));

    for (int i = 0; i < GOAL_COUNT; ++i) {
        if (hasBit(g.goalsDone, i)) continue;
        if (statValue(g, GOALS[i].stat) < GOALS[i].target) continue;
        g.goalsDone |= (1u << i);
        log.push(Sev::Event, std::string("GOAL COMPLETE: ") + GOALS[i].name + " (+" +
                             std::to_string(GOALS[i].renown) + " renown)");
    }

    int titleAfter = titleIndex(renownOf(g));
    if (titleAfter != titleBefore) log.push(Sev::Event, std::string("New title: ") + TITLES[titleAfter].name + "!");
    // Record when each title was reached. This also catches titles reached without an announcement,
    // like a save that loads already past a threshold that moved.
    for (int t = 1; t <= titleAfter; ++t)
        if (g.titleReachedAt[t] == 0) g.titleReachedAt[t] = std::max(1LL, g.playSeconds);
    for (const Note& n : syncUnlocks(g)) log.push(n);

    if (g.strain != Strain::None && !g.cured && g.orreryStage >= ORRERY_STAGE_COUNT) {
        g.cured = true;
        log.push(Sev::Event, std::string("CURED! The Orrery burned the ") + STRAINS[(int)g.strain].name +
                             " out of this profile. It's gone for good.");
    }
}

// ----- Stats and history -----
void recordStats(GameState& g) {
    g.bestEarnRate = std::max(g.bestEarnRate, toTokens(recentEarnRate(g)));
    if (g.playSeconds % STATS_EVERY != 0) return;
    g.history.push_back({g.playSeconds, g.lifetimeEarned, g.tokens, g.shards});
    if (g.history.size() > STATS_HISTORY_MAX) {
        // Thin the old part out: keep every other sample, and always the newest.
        std::vector<HistorySample> kept;
        for (size_t i = 0; i + 1 < g.history.size(); i += 2) kept.push_back(g.history[i]);
        kept.push_back(g.history.back());
        g.history = std::move(kept);
    }
}

std::vector<long long> historyColumns(const GameState& g, int columns) {
    std::vector<long long> out;
    if (columns <= 0 || g.history.empty()) return out;
    long long first = g.history.front().playSeconds, last = std::max(g.playSeconds, first + 1);
    size_t at = 0;
    for (int c = 0; c < columns; ++c) {
        long long when = first + (last - first) * c / std::max(1, columns - 1);
        while (at + 1 < g.history.size() && g.history[at + 1].playSeconds <= when) ++at;
        out.push_back(c == columns - 1 ? g.lifetimeEarned : g.history[at].lifetime);
    }
    return out;
}

// ----- Unlocks -----
bool isUnlocked(const GameState& g, Feature f) { return hasBit(g.unlocked, (int)f); }

// Features with no title (FeatureInfo::title < 0) open on their own terms.
static bool specialFeatureReady(const GameState& g, int f) {
    switch ((Feature)f) {
        case Feature::Stars: return g.orreryStage >= ORRERY_STAGE_COUNT;   // the Sun lights the sky
        default:             return false;
    }
}

std::string lockedText(const GameState& g, Feature f) {
    const FeatureInfo& info = FEATURES[(int)f];
    std::string name = info.name;
    name[0] = (char)std::toupper((unsigned char)name[0]);
    if (info.title < 0) return name + " open when the Orrery is complete.";
    const Title& t = TITLES[info.title];
    return name + " opens at the title " + t.name + " (" + std::to_string(t.renown) + " renown; you have " +
           std::to_string(renownOf(g)) + "). Goals earn renown.";
}

int nextUnlock(const GameState& g) {
    int best = -1;
    for (int i = 0; i < FEATURE_COUNT; ++i) {
        if (hasBit(g.unlocked, i) || FEATURES[i].title < 0) continue;   // the special ones have no title to aim for
        if (best < 0 || FEATURES[i].title < FEATURES[best].title) best = i;
    }
    return best;
}

Notes syncUnlocks(GameState& g) {
    Notes out;
    int ti = titleIndex(renownOf(g));
    for (int i = 0; i < FEATURE_COUNT; ++i) {
        if (hasBit(g.unlocked, i)) continue;
        if (FEATURES[i].title < 0 ? !specialFeatureReady(g, i) : FEATURES[i].title > ti) continue;
        g.unlocked |= 1u << i;
        std::string name = FEATURES[i].name;
        name[0] = (char)std::toupper((unsigned char)name[0]);
        out.push_back({Sev::Event, "UNLOCKED: " + name + ", " + FEATURES[i].blurb + ". (" + FEATURES[i].id + ")"});
    }
    return out;
}

int featureOfStat(Stat st) {
    switch (st) {
        case Stat::CuriosOwned:
        case Stat::ShopCurios:    return (int)Feature::Curios;
        case Stat::MonolithStage: return (int)Feature::Monolith;
        case Stat::ShardsTraded:  return (int)Feature::Market;
        case Stat::ContractsDone:
        case Stat::BestStanding:  return (int)Feature::Contracts;
        case Stat::CrewHired:
        case Stat::CrewMined:
        case Stat::CrewSize:
        case Stat::Expeditions:   return (int)Feature::Crew;
        case Stat::OrreryStage:   return (int)Feature::Orrery;
        case Stat::GadgetsBuilt:  return (int)Feature::Workshop;
        default:                  return -1;
    }
}

void grandfatherUnlocks(GameState& g) {
    // Only saves from before v7 need this, and those can't have anything newer than the Orrery.
    bool used[] = {
        g.curiosOwned != 0,
        g.monolithStage > 0 || g.monolithProgress > 0,
        g.shardsTraded > 0 || g.shards > 0,
        g.contractsDone > 0 || g.contractsFailed > 0 || !g.contracts.empty(),
        g.crewHired > 0 || !g.crew.empty(),
        g.orreryStage > 0 || g.orreryProgress > 0,
    };
    // Open everything up to the latest feature in use, so the unlocked set has no gaps.
    int last = -1;
    for (int i = 0; i < (int)std::size(used); ++i) if (used[i]) last = i;
    for (int i = 0; i <= last; ++i) g.unlocked |= 1u << i;
}
