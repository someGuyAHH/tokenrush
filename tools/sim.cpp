// Headless balance simulator. It links the game's core (no UI, no threads) and plays it
// with a simple scripted player, fast-forwarding time one second per loop. Use it to catch
// exploit loops and to check that new systems pay off without taking over the game.
//
//   make sim
//   ./sim [hours] [seeds] [strategy] [minutes between table rows, default 30]
//
// Strategies:
//   normal   mines, upgrades, takes contracts, hires crew, trades a little   (default)
//   trader   only trades shards on the market, starting from 50,000 tokens
//   idle     never mines and takes no contracts; starts with 5,000 tokens and relies on
//            upgrades, crew and the market
//   noecon   the old game: mining and upgrades only, as a baseline

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "core/actions.hpp"
#include "core/contracts.hpp"
#include "core/crew.hpp"
#include "core/disasters.hpp"
#include "core/game.hpp"
#include "core/market.hpp"
#include "core/ticks.hpp"
#include "core/util.hpp"

namespace {

enum class Strategy { Normal, Trader, Idle, NoEcon };

struct Tally {
    std::string curioTimes;      // " 1@3m 2@5m ...": when each curio was bought
    long long tradeProfit = 0;   // tokens from selling minus tokens spent buying
    int maxHeat = 0;
    int quits = 0;
};

struct Sim {
    GameState g;
    MessageLog log;
    std::mt19937 rng;
    AutomationClocks clocks;
    std::time_t t = 1'800'000'000;
    Strategy strat;
    Tally tally;
    double mineRate = 0.5;   // manual mines per second (a steady player, not a frantic one)

    Sim(unsigned seed, Strategy s) : rng(seed), strat(s) {
        g.lastUpdate = t;
        if (s == Strategy::Trader) g.tokens = 50'000;
        if (s == Strategy::Idle) g.tokens = 5'000;
        // trader and idle test the economy itself, so they skip the title unlocks
        if (s == Strategy::Trader || s == Strategy::Idle) g.unlocked = (1u << FEATURE_COUNT) - 1;
    }

    void buy(long long n) {
        long long before = g.tokens;
        buyShards(g, n);
        tally.tradeProfit -= before - g.tokens;
    }
    void sell(long long n) {
        long long before = g.tokens;
        sellShards(g, n);
        tally.tradeProfit += g.tokens - before;
    }

    void player() {
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        bool econ = strat == Strategy::Normal || strat == Strategy::Idle;

        // Chasing thieves: mine as fast as you can.
        bool chasing = disasterPending(g) && g.disaster.chasing;
        if ((strat == Strategy::Normal || strat == Strategy::NoEcon) && (chasing || uni(rng) < mineRate)) mine(g, t, rng);
        if (strat == Strategy::Normal) disasterPlan();

        // Upgrades: buy the cheapest one we can afford, every few seconds.
        if (strat != Strategy::Trader && t % 5 == 0) {
            auto ups = upgrades(g);
            size_t best = 0;
            for (size_t i = 1; i < ups.size(); ++i) if (ups[i].cost < ups[best].cost) best = i;
            long long reserve = econ ? (long long)(g.shardPrice * 15) : 0;   // keep some for shards
            if (g.tokens - ups[best].cost >= reserve) buyUpgrade(g, best);
        }

        if (econ && t % 10 == 0) crewPlan();
        if (strat == Strategy::Normal && t % 10 == 0) contractPlan();
        if (strat == Strategy::Normal && t % 30 == 0) buildMonolithWhenRich();
        if (strat == Strategy::Normal && t % 20 == 0) curioPlan();
        if (strat == Strategy::Normal && t % 60 == 0) orreryPlan();
        if (strat == Strategy::Trader && t % 5 == 0) traderPlan();
    }

    void crewPlan() {
        // Keep enough shards for a payday and a stake, then hire, then train.
        long long keep = totalWages(g) + 10 + held();
        if ((int)g.crew.size() < crewCap(g)) {
            for (size_t i = 0; i < g.candidates.size(); ++i) {
                long long cost = hireCost(g, g.candidates[i]);
                if (g.shards >= cost + keep) { hire(g, (int)i); return; }
                long long need = cost + keep - g.shards;
                if (need > 0 && g.shardPrice < SHARD_FAIR_PRICE * 1.05 && buyCost(g, need) < g.tokens / 4) { buy(need); return; }
            }
        }
        for (size_t i = 0; i < g.crew.size(); ++i) {
            if (g.crew[i].level < levelCap(g, g.crew[i]) && g.shards >= trainCost(g.crew[i]) + keep) { train(g, (int)i); return; }
        }
        // A sensible player promotes a few times, then stops when it gets silly.
        for (size_t i = 0; i < g.crew.size(); ++i) {
            const CrewMember& c = g.crew[i];
            if (c.level >= levelCap(g, c) && c.stars < 3 && g.shards >= promoteCost(c) + keep) { promote(g, (int)i); return; }
        }
        if (g.shards < keep && buyCost(g, keep - g.shards) < g.tokens / 4) buy(keep - g.shards);
        // Sell clear surplus when the price is good.
        long long surplus = g.shards - keep - 60;
        bool orreryDone = g.orreryStage >= ORRERY_STAGE_COUNT;   // until then, surplus shards go into it
        if (orreryDone && surplus > 0 && g.shardPrice > SHARD_FAIR_PRICE * 1.1) sell(std::min(surplus, 20LL));
    }

    // Shards a running Hold job needs kept on hand.
    long long held() const {
        long long n = 0;
        for (const Contract& c : g.contracts) if (c.kind == ContractKind::Hold) n += c.target;
        return n;
    }

    void contractPlan() {
        // Work a Trade job: round trips of 40 shards.
        for (const Contract& c : g.contracts) {
            if (c.kind != ContractKind::Trade || buyCost(g, 40) > g.tokens / 3) continue;
            buy(40);
            sell(40);
        }
        if ((int)g.contracts.size() >= contractSlots(g)) return;
        for (size_t i = 0; i < g.offers.size(); ++i) {
            if (g.offers[i].stake > g.shards) continue;
            // This player mines all the time and has no plan for heat: those two jobs are hopeless.
            if (g.offers[i].kind == ContractKind::HandsOff || g.offers[i].kind == ContractKind::CoolOff) continue;
            // Shady money adds heat: a sensible player stops taking it once audits are possible.
            if (CLIENTS[g.offers[i].client].style == ClientStyle::Shady && g.heat >= AUDIT_MIN_HEAT) continue;
            size_t before = g.contracts.size();
            acceptContract(g, (int)i);   // takes whatever comes first: a careless player on purpose
            if (g.contracts.size() > before) return;
        }
    }

    // Once the Monolith is done, pour shards into the Orrery: buy a batch whenever the price
    // has settled, spending up to half the tokens, and set everything above a reserve.
    void orreryPlan() {
        if (g.orreryStage >= ORRERY_STAGE_COUNT || g.monolithStage < MONO_STAGE_COUNT) return;
        if (g.shardPrice < SHARD_FAIR_PRICE * 1.3) {
            long long budget = g.tokens / 2, lo = 0, hi = MARKET_MAX_ORDER;
            while (lo < hi) { long long mid = (lo + hi + 1) / 2; if (buyCost(g, mid) <= budget) lo = mid; else hi = mid - 1; }
            if (lo > 0) buy(lo);
        }
        long long keep = totalWages(g) * 2 + 20 + held();
        if (g.shards > keep) buildOrrery(g, g.shards - keep);
    }

    // Buys the cheapest curio still missing once it costs under a quarter of the wallet.
    void curioPlan() {
        if (!isUnlocked(g, Feature::Curios)) return;
        int best = -1;
        for (int i = 0; i < SHOP_CURIO_COUNT; ++i)   // the Dealer's aren't for sale
            if (!hasBit(g.curiosOwned, i) && (best < 0 || CURIOS[i].cost < CURIOS[best].cost)) best = i;
        if (best >= 0 && CURIOS[best].cost * 4 <= g.tokens) {
            buyCurio(g, best);
            tally.curioTimes += " " + std::to_string(std::popcount(g.curiosOwned)) + "@" + std::to_string(g.playSeconds / 60) + "m";
        }
    }

    // A player who's around stops most disasters (when the warning has been up a few seconds),
    // and repairs whatever breaks once it's affordable.
    void disasterPlan() {
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        if (disasterPending(g) && !g.disaster.chasing && g.disaster.secondsLeft == DISASTERS[g.disaster.kind].warnSeconds - 5) {
            if (uni(rng) < 0.7) respondDisaster(g);
        }
        if (t % 30 == 0 && anythingBroken(g)) {
            for (const Repair& r : repairs(g))
                if (r.tokens * 3 <= g.tokens && r.shards + totalWages(g) <= g.shards) { repair(g, -1); break; }
        }
    }

    void buildMonolithWhenRich() {
        if (g.monolithStage < MONO_STAGE_COUNT && g.tokens > MONO[g.monolithStage].cost * 3)
            buildMonolith(g, MONO[g.monolithStage].cost - g.monolithProgress);
    }

    void traderPlan() {
        // Buy the dips, sell the rips. The best a mean-reversion trader can do here.
        if (g.shardPrice < SHARD_FAIR_PRICE * 0.88) {
            long long n = std::min<long long>(maxAffordable(g), 40);
            if (n > 0) buy(n);
        } else if (g.shardPrice > SHARD_FAIR_PRICE * 1.10 && g.shards > 0) {
            sell(std::min<long long>(g.shards, 40));
        }
    }

    void second() {
        size_t crewBefore = g.crew.size();

        player();
        worldTick(g, log, t, rng);
        marketTick(g, log, rng);
        automationTick(g, log, t, rng, clocks);
        crewTick(g, log, t, rng);
        checkContract(g, log);
        checkDisaster(g, log);
        checkGoals(g, log);

        if (g.crew.size() < crewBefore) tally.quits += (int)(crewBefore - g.crew.size());
        tally.maxHeat = std::max(tally.maxHeat, g.heat);
        ++t;
    }
};

const char* name(Strategy s) {
    switch (s) {
        case Strategy::Normal: return "normal";
        case Strategy::Trader: return "trader";
        case Strategy::Idle:   return "idle";
        case Strategy::NoEcon: return "noecon";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    double hours = argc > 1 ? std::atof(argv[1]) : 4.0;
    int seeds = argc > 2 ? std::atoi(argv[2]) : 5;
    std::string sarg = argc > 3 ? argv[3] : "normal";
    long long every = argc > 4 ? std::max(1, std::atoi(argv[4])) * 60LL : 1800;   // table row interval, in minutes
    Strategy strat = sarg == "trader" ? Strategy::Trader : sarg == "idle" ? Strategy::Idle
                   : sarg == "noecon" ? Strategy::NoEcon : Strategy::Normal;

    long long total = (long long)(hours * 3600);
    std::printf("strategy %s, %.1f hours, %d seeds\n", name(strat), hours, seeds);

    for (int seed = 1; seed <= seeds; ++seed) {
        Sim sim((unsigned)seed * 7919u, strat);
        std::printf("\nseed %d\n  %5s %12s %12s %7s %7s %6s %5s %5s %5s %8s %6s %6s %5s\n", seed, "hour", "tokens", "lifetime",
                    "shards", "price", "done", "fail", "heat", "crew", "crewMined", "audits", "renown", "dis");
        int lastStage = 0;
        std::string stageTimes, unlockTimes;
        std::uint32_t lastUnlocked = sim.g.unlocked;
        for (long long s = 1; s <= total; ++s) {
            sim.second();
            if (sim.g.orreryStage != lastStage) {
                lastStage = sim.g.orreryStage;
                char buf[64];
                std::snprintf(buf, sizeof buf, " %d@%.1fh", lastStage, s / 3600.0);
                stageTimes += buf;
            }
            if (sim.g.unlocked != lastUnlocked) {
                for (int f = 0; f < FEATURE_COUNT; ++f) {
                    if (!(sim.g.unlocked >> f & 1u) || (lastUnlocked >> f & 1u)) continue;
                    char buf[96];
                    std::snprintf(buf, sizeof buf, "  %s@%.0fm", FEATURES[f].id, s / 60.0);
                    unlockTimes += buf;
                }
                lastUnlocked = sim.g.unlocked;
            }
            if (s % every == 0 || s == total) {
                const GameState& g = sim.g;
                std::printf("  %5.1f %12lld %12lld %7lld %7.1f %6lld %5lld %5d %5zu %9lld %6lld %6d %2lld/%-2lld\n", s / 3600.0,
                            g.tokens, g.lifetimeEarned, g.shards, g.shardPrice, g.contractsDone, g.contractsFailed,
                            g.heat, g.crew.size(), g.crewMined, g.audits, renownOf(g), g.disastersStopped, g.disastersHit);
            }
        }
        const GameState& g = sim.g;
        std::string crew;
        for (const CrewMember& c : g.crew) crew += std::string(" ") + ROLES[(int)c.role].name + "L" + std::to_string(c.level) + starsText(c);
        std::printf("  unlocks:%s\n  renown %d (%s), goals %d/%d\n", unlockTimes.empty() ? " none" : unlockTimes.c_str(),
                    renownOf(g), TITLES[titleIndex(renownOf(g))].name, std::popcount(g.goalsDone), GOAL_COUNT);
        std::printf("  curios:%s\n", sim.tally.curioTimes.empty() ? " none" : sim.tally.curioTimes.c_str());
        std::printf("  monolith done: %s, orrery stages:%s\n", g.monolithStage >= MONO_STAGE_COUNT ? "yes" : "no",
                    stageTimes.empty() ? " none" : stageTimes.c_str());
        std::printf("  trade profit %lld, max heat %d, crew quits %d, upgrades P%d M%d D%d B%d, crew:%s\n",
                    sim.tally.tradeProfit, sim.tally.maxHeat, sim.tally.quits, g.pickaxe, g.magnet, g.drone, g.bot,
                    crew.c_str());
    }
    return 0;
}
