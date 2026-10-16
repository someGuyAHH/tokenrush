#include "ticks.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "choices.hpp"
#include "contracts.hpp"
#include "crew.hpp"
#include "disasters.hpp"
#include "tasks.hpp"
#include "game.hpp"
#include "stars.hpp"
#include "util.hpp"

void worldTick(GameState& g, MessageLog& log, std::time_t t, std::mt19937& rng) {
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    updatePassive(g, t);
    g.playSeconds += 1;
    updateRates(g);

    if (g.streak > 0 && g.lastMine != 0 && std::difftime(t, g.lastMine) >= (double)streakTimeout(g)) {
        log.push(Sev::Info, "Your streak of " + std::to_string(g.streak) + " expired.");
        g.streak = 0;
    }

    if (g.event != EV_NONE && t >= g.eventEnds) {
        log.push(Sev::Info, std::string(EVENTS[g.event].name) + " ended.");
        g.event = EV_NONE;
        g.eventEnds = 0;
    } else if (g.event == EV_NONE && uni(rng) < EVENT_CHANCE_PER_SEC * (gadgetOn(g, Gadget::LightningRod) ? 2.0 : 1.0)) {
        std::vector<int> pool = {EV_METEOR, EV_OVERCLOCK, EV_GOLDRUSH};
        if (isUnlocked(g, Feature::Crew) && !g.crew.empty()) pool.push_back(EV_PAYDAY);   // needs someone to rush
        int e = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)];
        int lasts = EVENTS[e].duration;
        g.event = e;
        g.eventEnds = t + lasts;
        g.eventsSeen += 1;
        std::string extra;
        if (e == EV_PAYDAY) {   // ...and they want paying when it's over
            g.paydayClock = std::min(g.paydayClock, lasts);
            extra = " Payday is in " + duration(g.paydayClock) + ": have " + count(totalWages(g), "shard") + " ready.";
        }
        log.push(Sev::Event, std::string("EVENT: ") + EVENTS[e].name + " for " +
                             std::to_string(lasts) + "s! (" + EVENTS[e].desc + ")" + extra, LogKind::Events);
    }

    if (infected(g, Strain::Leech) && g.playSeconds % 300 == 150 && g.tokens >= 100) {
        long long drained = g.tokens / 100;
        g.tokens -= drained;
        log.push(Sev::Bad, "The Leech drank " + compact(drained) + " tokens.");
    }

    contractsTick(g, log, rng);
    choicesTick(g, log, rng);
    disastersTick(g, log, rng);
    tasksTick(g, log);
    hourglassTick(g);
    recordStats(g);
    checkGoals(g, log);
}

static long long botMine(GameState& g, std::time_t t, std::mt19937& rng) {
    EventFx fx = fxNow(g, t);
    std::uniform_real_distribution<double> roll(0.0, 1.0);

    if (hasResonance(g)) {
        // Resonance: the bot rolls bonuses and jackpots exactly like a manual mine, scaled down.
        double base = baseYield(g);
        double r = roll(rng);
        double jackpotP = jackpotChance(g, fx.odds);
        double bonusP   = bonusChance(g, fx.odds);
        double swing = base * 1.5;
        if (r < jackpotP) {
            swing += base * jackpotSize(g) * jackpotBoost(g, fx.odds);
            if (gadgetOn(g, Gadget::LuckyPenny)) g.shards += std::uniform_int_distribution<int>(1, 3)(rng);
        } else if (r < jackpotP + bonusP) {
            swing += base * bonusSize(g) * bonusBoost(g, fx.odds);
        }
        return toTokens(swing * resonantBotPower(g) * fx.mining * humBoost(g));
    }

    double gain = baseYield(g) * (0.6 + 0.2 * g.bot);

    std::uniform_real_distribution<double> uni(0.0, 1.0);
    double raw = 0.03 + 0.02 * g.magnet;
    double bonusChance = std::min(0.35, raw * fx.odds);
    double boost = fx.odds * std::min(0.35, raw) / std::max(1e-9, bonusChance);   // Gold Rush past the cap pays more instead
    if (uni(rng) < bonusChance) gain += baseYield(g) * (4.0 + 2.0 * g.magnet) * boost;

    return toTokens(gain * fx.mining * humBoost(g));
}

static void autoBuy(GameState& g, MessageLog& log) {
    long long cost = 0;
    const char* name = autoBuyOnce(g, cost);
    if (!name) return;
    int level = 0;
    for (const UpgradeRef& u : upgrades(g))
        if (u.name == std::string(name)) level = *u.lvl;
    log.push(Sev::Info, std::string("Auto-Buyer bought ") + name + " -> lvl " + std::to_string(level) + " (cost " + compact(cost) + ")", LogKind::AutoBuyer);
}

void automationTick(GameState& g, MessageLog& log, std::time_t t, std::mt19937& rng, AutomationClocks& c) {
    if (g.bot > 0 && !g.botBroken && g.machinesPaused <= 0 && ++c.bot >= botIntervalOf(g)) {
        c.bot = 0;
        long long gain = botMine(g, t, rng);
        earn(g, gain, Source::Bot);
        g.botTotal = addTokens(g.botTotal, gain);
        c.minedSinceReport = addTokens(c.minedSinceReport, gain);
    }

    if (++c.report >= BOT_REPORT_SECONDS) {
        c.report = 0;
        if (c.minedSinceReport > 0) {
            log.push(Sev::Info, "Mining Bot mined +" + compact(c.minedSinceReport) +
                                " tokens in the last " + std::to_string(BOT_REPORT_SECONDS) + "s.", LogKind::Bot);
            c.minedSinceReport = 0;
        }
    }

    if (g.autoUnlocked && g.autoOn && ++c.buy >= AUTOBUY_SECONDS) {
        c.buy = 0;
        autoBuy(g, log);
    }
}
