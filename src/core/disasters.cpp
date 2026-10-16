#include "disasters.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "crew.hpp"
#include "game.hpp"
#include "util.hpp"

const DisasterInfo DISASTERS[] = {
    {"Power Surge",   "The grid is humming and the lights are flickering. A power surge is coming.", 20, "brace",
     "pull the breakers: Drones and the Bot stop for a minute"},
    {"Bot Breakdown", "The Mining Bot is grinding and smoking.",                                   20, "oil",
     "oil it before it seizes up"},
    {"Vault Fire",    "Smoke is pouring out of the shard vault!",                                  20, "douse",
     "pay the firefighters to put it out"},
    {"Thieves",       "Someone's been seen sneaking around the token vault.",                      25, "chase",
     "chase them: mine 15 times by hand before they get away"},
    {"Quake",         "The ground is trembling under the Orrery.",                                 30, "brace",
     "shore it up with a few shards"},
    {"Short Circuit", "Sparks are coming off your gadgets.",                                       20, "unplug",
     "unplug everything: gadgets stop for two minutes"},
};
static_assert(std::size(DISASTERS) == DISASTER_KIND_COUNT);

namespace {

double income(const GameState& g) { return std::max(g.incomeRate, incomeEstimate(g)); }

// What an equipped, working gadget could be hit by a Short Circuit.
std::vector<int> pluggedIn(const GameState& g) {
    std::vector<int> out;
    for (int x : g.gadgetSlots)
        if (x >= 0 && hasBit(g.gadgetsOwned, x) && !hasBit(g.gadgetsBroken, x)) out.push_back(x);
    return out;
}

// Built Orrery stages a Quake can crack (the Sun is too sturdy, and has no perk to lose).
std::vector<int> crackable(const GameState& g) {
    std::vector<int> out;
    for (int i = 0; i < std::min(g.orreryStage, ORRERY_STAGE_COUNT - 1); ++i)
        if (!hasBit(g.orreryCracked, i)) out.push_back(i);
    return out;
}

bool possible(const GameState& g, DisasterKind k) {
    switch (k) {
        case DisasterKind::Surge:    return g.drone >= 2 && g.dronesDown == 0;
        case DisasterKind::BotBreak: return g.bot >= 1 && !g.botBroken;
        case DisasterKind::Fire:     return g.shards >= 100;
        case DisasterKind::Thieves:  return g.tokens >= 5000;
        case DisasterKind::Quake:    return !crackable(g).empty();
        case DisasterKind::Short:    return !pluggedIn(g).empty() && g.gadgetsUnplugged <= 0;
        case DisasterKind::Count:    break;
    }
    return false;
}

void resetClock(GameState& g, std::mt19937& rng) {
    g.disasterClock = std::uniform_int_distribution<int>(DISASTER_EVERY_MIN, DISASTER_EVERY_MAX)(rng);
    if (gadgetOn(g, Gadget::LightningRod)) g.disasterClock = g.disasterClock * 4 / 5;   // it draws trouble too
}

long long botRepairCost(const GameState& g) {
    GameState probe = g;   // upgrades() wants a non-const state (and knows about Rust)
    return std::max(100LL, toTokens((double)upgrades(probe)[3].cost * 0.15 * fixerSoften(g)));
}
long long stageRepairCost(const GameState& g, int stage) { return std::max(10LL, toTokens((double)ORRERY[stage].cost * 0.10 * fixerSoften(g))); }
long long gadgetRepairCost(const GameState& g, int x) { return std::max(10LL, toTokens((double)GADGETS[x].cost * 0.5 * fixerSoften(g))); }

// The warning ran out: it hits.
void strike(GameState& g, MessageLog& log, std::mt19937& rng) {
    Disaster d = g.disaster;
    g.disaster = Disaster{};
    g.disastersHit += 1;
    g.stoppedInRow = 0;
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    double soft = fixerSoften(g);
    std::string fix = soft < 1.0 ? " (your Fixers kept it down)" : "";

    switch ((DisasterKind)d.kind) {
        case DisasterKind::Surge:
            g.dronesDown = std::max(1, g.drone / 2);
            g.dronesDownSecs = (int)std::lround(SURGE_SECONDS * soft);
            log.push(Sev::Bad, "POWER SURGE! " + num(g.dronesDown) + " Drones burnt out. They'll be back in " + duration(g.dronesDownSecs) + fix + ".");
            break;
        case DisasterKind::BotBreak:
            g.botBroken = true;
            log.push(Sev::Bad, "The Mining Bot seized up and stopped. Repair it for " + num(botRepairCost(g)) + " tokens ('repair').");
            break;
        case DisasterKind::Fire: {
            long long burnt = (long long)std::floor((double)g.shards * (0.08 + 0.07 * uni(rng)) * soft);
            g.shards -= burnt;
            log.push(Sev::Bad, "VAULT FIRE! " + count(burnt, "shard") + " went up in smoke" + fix + ".");
            break;
        }
        case DisasterKind::Thieves: {
            long long stolen = (long long)std::floor((double)g.tokens * (0.05 + 0.05 * uni(rng)) * soft);
            g.tokens -= stolen;
            log.push(Sev::Bad, std::string(d.chasing ? "They got away. " : "THIEVES! ") + num(stolen) + " tokens are gone" + fix + ".");
            break;
        }
        case DisasterKind::Quake: {
            int st = d.target;
            if (st < 0 || st >= g.orreryStage || hasBit(g.orreryCracked, st)) break;
            g.orreryCracked |= 1u << st;
            log.push(Sev::Bad, std::string("QUAKE! The Orrery's ") + ORRERY[st].name + " cracked: " + ORRERY[st].perk +
                               " is off until you repair it (" + num(stageRepairCost(g, st)) + " shards, 'repair').");
            break;
        }
        case DisasterKind::Short: {
            int x = d.target;
            if (x < 0 || !hasBit(g.gadgetsOwned, x) || hasBit(g.gadgetsBroken, x)) break;
            g.gadgetsBroken |= 1u << x;
            log.push(Sev::Bad, std::string("SHORT CIRCUIT! The ") + GADGETS[x].name + " burnt out. It's dead until you repair it (" +
                               num(gadgetRepairCost(g, x)) + " shards, 'repair').");
            break;
        }
        case DisasterKind::Count:
            break;
    }
}

void stopped(GameState& g, const std::string& how, Notes& out) {
    g.disaster = Disaster{};
    g.disastersStopped += 1;
    g.stoppedInRow += 1;
    if (g.disastersStopped >= 5) spotSecret(g, Secret::Firefighter);
    out.push_back({Sev::Good, how});
}

}  // namespace

bool disasterPending(const GameState& g) { return g.disaster.kind >= 0; }

double fixerSoften(const GameState& g) { return 1.0 - std::min(0.5, 0.05 * roleLevels(g, Role::Fixer)); }

bool startDisaster(GameState& g, DisasterKind k, std::mt19937& rng) {
    if (disasterPending(g) || !possible(g, k)) return false;
    Disaster d;
    d.kind = (int)k;
    d.secondsLeft = DISASTERS[(int)k].warnSeconds;
    switch (k) {
        case DisasterKind::BotBreak: d.tokens = std::max(200LL, toTokens(botRatePerSec(g) * 60.0 + income(g) * 10.0)); break;
        case DisasterKind::Fire:     d.tokens = std::max(500LL, toTokens(income(g) * 20.0)); break;
        case DisasterKind::Quake: {
            std::vector<int> st = crackable(g);
            d.target = st[std::uniform_int_distribution<size_t>(0, st.size() - 1)(rng)];
            d.shards = std::max(10LL, toTokens(std::ceil((double)ORRERY[d.target].cost * 0.02)));
            break;
        }
        case DisasterKind::Short: {
            std::vector<int> on = pluggedIn(g);
            d.target = on[std::uniform_int_distribution<size_t>(0, on.size() - 1)(rng)];
            break;
        }
        default: break;
    }
    g.disaster = d;
    return true;
}

std::string disasterText(const GameState& g) {
    const Disaster& d = g.disaster;
    if (d.kind < 0) return "";
    const DisasterInfo& info = DISASTERS[d.kind];
    std::string cost;
    if (d.tokens > 0) cost = " (" + num(d.tokens) + " tokens)";
    if (d.shards > 0) cost = " (" + count(d.shards, "shard") + ")";
    std::string stake;
    switch ((DisasterKind)d.kind) {
        case DisasterKind::Surge:    stake = "half your Drones burn out for a while"; break;
        case DisasterKind::BotBreak: stake = "the Bot stops until you repair it"; break;
        case DisasterKind::Fire:     stake = "8-15% of your shards burn"; break;
        case DisasterKind::Thieves:  stake = "5-10% of your tokens are stolen"; break;
        case DisasterKind::Quake:    stake = std::string("the ") + ORRERY[std::max(0, d.target)].name + " cracks and its perk is off until repaired"; break;
        case DisasterKind::Short:    stake = std::string("the ") + GADGETS[std::max(0, d.target)].name + " burns out until repaired"; break;
        case DisasterKind::Count:    break;
    }
    if (d.chasing) {
        return std::string(info.warning) + " You're after them: " + std::to_string(g.manualMines - d.chaseStart) + "/" +
               std::to_string(CHASE_MINES) + " mines. If they get away, " + stake + ".";
    }
    return std::string(info.warning) + " '" + info.verb + "' to " + info.action + cost + ". Otherwise " + stake + ".";
}

Notes respondDisaster(GameState& g) {
    Disaster& d = g.disaster;
    if (d.kind < 0) return {{Sev::Info, "Nothing's going wrong right now."}};
    Notes out;
    switch ((DisasterKind)d.kind) {
        case DisasterKind::Surge:
            g.machinesPaused = PAUSE_SECONDS;
            stopped(g, "You pulled the breakers. The surge fizzles out harmlessly; Drones and the Bot are back on in a minute.", out);
            break;
        case DisasterKind::BotBreak:
        case DisasterKind::Fire:
            if (g.tokens < d.tokens) return {{Sev::Warn, "That takes " + num(d.tokens) + " tokens. You have " + num(g.tokens) + "."}};
            g.tokens -= d.tokens;
            stopped(g, d.kind == (int)DisasterKind::Fire ? "The firefighters put it out. Not a shard lost."
                                                         : "A few drops of oil and the Bot purrs again.", out);
            break;
        case DisasterKind::Thieves:
            if (d.chasing) return {{Sev::Info, "You're already after them: keep mining! (" + std::to_string(g.manualMines - d.chaseStart) + "/" +
                                               std::to_string(CHASE_MINES) + ")"}};
            d.chasing = true;
            d.chaseStart = g.manualMines;
            out.push_back({Sev::Event, "After them! Mine " + std::to_string(CHASE_MINES) + " times in the next " + duration(d.secondsLeft) + "."});
            break;
        case DisasterKind::Quake:
            if (g.shards < d.shards) return {{Sev::Warn, "Shoring it up takes " + count(d.shards, "shard") + ". You have " + num(g.shards) + "."}};
            g.shards -= d.shards;
            stopped(g, "You shored up the Orrery. The ground stops shaking and nothing cracks.", out);
            break;
        case DisasterKind::Short:
            g.gadgetsUnplugged = UNPLUG_SECONDS;
            stopped(g, "You pulled every plug. The sparks die down; your gadgets are back on in two minutes.", out);
            break;
        case DisasterKind::Count:
            break;
    }
    return out;
}

void checkDisaster(GameState& g, MessageLog& log) {
    Disaster& d = g.disaster;
    if (d.kind != (int)DisasterKind::Thieves || !d.chasing || g.manualMines - d.chaseStart < CHASE_MINES) return;
    Notes out;
    stopped(g, "You caught the thieves! Every token is back where it belongs.", out);
    for (const Note& n : out) log.push(n);
}

void disastersTick(GameState& g, MessageLog& log, std::mt19937& rng) {
    // The temporary kinds of damage wear off.
    if (g.dronesDownSecs > 0 && --g.dronesDownSecs == 0) {
        log.push(Sev::Good, "The burnt-out Drones are running again.");
        g.dronesDown = 0;
    }
    if (g.machinesPaused > 0 && --g.machinesPaused == 0) log.push(Sev::Info, "Breakers back on: Drones and the Bot are running.");
    if (g.gadgetsUnplugged > 0 && --g.gadgetsUnplugged == 0) log.push(Sev::Info, "Your gadgets are plugged back in.");

    if (disasterPending(g)) {
        checkDisaster(g, log);
        if (disasterPending(g) && --g.disaster.secondsLeft <= 0) strike(g, log, rng);
        return;
    }
    if (!isUnlocked(g, Feature::Crew)) return;   // nothing goes wrong this early in the game

    if (g.disasterClock <= 0) {   // first tick: the first one comes a little sooner
        g.disasterClock = std::uniform_int_distribution<int>(DISASTER_EVERY_MIN / 2, DISASTER_EVERY_MIN)(rng);
        return;
    }
    if (--g.disasterClock > 0) return;
    resetClock(g, rng);

    std::vector<DisasterKind> pool;
    for (int k = 0; k < DISASTER_KIND_COUNT; ++k)
        if (possible(g, (DisasterKind)k)) pool.push_back((DisasterKind)k);
    if (pool.empty()) return;
    DisasterKind k = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)];
    if (startDisaster(g, k, rng)) {
        log.push(Sev::Bad, std::string("DISASTER: ") + DISASTERS[(int)k].name + "! " + disasterText(g) + " (" +
                           duration(g.disaster.secondsLeft) + ")");
    }
}

// ----- Repairs -----
std::vector<Repair> repairs(const GameState& g) {
    std::vector<Repair> out;
    if (g.botBroken) out.push_back({Repair::Bot, 0, "the Mining Bot", "it doesn't mine", botRepairCost(g), 0});
    for (int i = 0; i < ORRERY_STAGE_COUNT; ++i)
        if (hasBit(g.orreryCracked, i))
            out.push_back({Repair::Stage, i, std::string("the Orrery's ") + ORRERY[i].name, std::string(ORRERY[i].perk) + " is off", 0,
                           stageRepairCost(g, i)});
    for (int x = 0; x < GADGET_COUNT; ++x)
        if (hasBit(g.gadgetsBroken, x))
            out.push_back({Repair::Gadget, x, std::string("the ") + GADGETS[x].name, "it does nothing", 0, gadgetRepairCost(g, x)});
    return out;
}

bool anythingBroken(const GameState& g) { return g.botBroken || g.orreryCracked != 0 || g.gadgetsBroken != 0; }

long long gadgetRepairShards(const GameState& g, int gadget) { return gadgetRepairCost(g, gadget); }

int repairIndexOf(const GameState& g, Repair::Kind kind, int index) {
    std::vector<Repair> list = repairs(g);
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].kind == kind && list[i].index == index) return (int)i;
    return -1;
}

Notes gadgetTrouble(const GameState& g) {
    Notes out;
    if (disasterPending(g) && g.disaster.kind == (int)DisasterKind::Short) {
        int x = g.disaster.target;
        std::string what = x >= 0 && x < GADGET_COUNT ? std::string("the ") + GADGETS[x].name : std::string("a gadget");
        out.push_back({Sev::Bad, "SHORT CIRCUIT in " + duration(g.disaster.secondsLeft) + ": " + what +
                                     " burns out unless you unplug your gadgets ('respond', or x)."});
    }
    if (g.gadgetsUnplugged > 0)
        out.push_back({Sev::Warn, "Unplugged: every gadget is off for another " + duration(g.gadgetsUnplugged) + "."});
    for (int x = 0; x < GADGET_COUNT; ++x)
        if (hasBit(g.gadgetsBroken, x))
            out.push_back({Sev::Bad, std::string("The ") + GADGETS[x].name + " is burnt out and does nothing until it's repaired (" +
                                         num(gadgetRepairCost(g, x)) + " shards, 'repair')."});
    return out;
}

static void fixOne(GameState& g, const Repair& r) {
    switch (r.kind) {
        case Repair::Bot:    g.botBroken = false; break;
        case Repair::Stage:  g.orreryCracked &= ~(1u << r.index); break;
        case Repair::Gadget: g.gadgetsBroken &= ~(1u << r.index); break;
    }
}

Notes repair(GameState& g, int idx) {
    std::vector<Repair> list = repairs(g);
    if (list.empty()) return {{Sev::Info, "Nothing needs repairing."}};
    if (idx >= (int)list.size()) return {{Sev::Warn, "No such repair."}};
    Notes out;
    for (int i = 0; i < (int)list.size(); ++i) {
        if (idx >= 0 && i != idx) continue;
        const Repair& r = list[(size_t)i];
        if (g.tokens < r.tokens || g.shards < r.shards) {
            std::string need = r.tokens > 0 ? num(r.tokens) + " tokens" : count(r.shards, "shard");
            out.push_back({Sev::Warn, "Repairing " + r.what + " takes " + need + "."});
            continue;
        }
        g.tokens -= r.tokens;
        g.shards -= r.shards;
        fixOne(g, r);
        std::string paid = r.tokens > 0 ? num(r.tokens) + " tokens" : count(r.shards, "shard");
        std::string what = r.what;
        what[0] = (char)std::toupper((unsigned char)what[0]);
        out.push_back({Sev::Good, what + " is fixed (" + paid + ")."});
    }
    return out;
}
