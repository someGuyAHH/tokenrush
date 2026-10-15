#include "ticks.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "choices.hpp"
#include "contracts.hpp"
#include "game.hpp"
#include "util.hpp"

void worldTick(GameState& g, MessageLog& log, std::time_t t, std::mt19937& rng) {
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::uniform_int_distribution<int> pickEvent(1, EV_COUNT - 1);

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
    } else if (g.event == EV_NONE && uni(rng) < EVENT_CHANCE_PER_SEC) {
        int e = pickEvent(rng);
        g.event = e;
        g.eventEnds = t + EVENTS[e].duration;
        g.eventsSeen += 1;
        log.push(Sev::Event, std::string("EVENT: ") + EVENTS[e].name + " for " +
                             std::to_string(EVENTS[e].duration) + "s! (" + EVENTS[e].desc + ")");
    }

    if (infected(g, Strain::Leech) && g.playSeconds % 300 == 150 && g.tokens >= 100) {
        long long drained = g.tokens / 100;
        g.tokens -= drained;
        log.push(Sev::Bad, "The Leech drank " + num(drained) + " tokens.");
    }

    contractsTick(g, log, rng);
    choicesTick(g, log, rng);
    recordStats(g);
    checkGoals(g, log);
}

static long long botMine(const GameState& g, std::time_t t, std::mt19937& rng) {
    EventFx fx = fxNow(g, t);
    std::uniform_real_distribution<double> roll(0.0, 1.0);

    if (hasResonance(g)) {
        // Resonance: the bot rolls bonuses and jackpots exactly like a manual mine, scaled down.
        double base = baseYield(g);
        double r = roll(rng);
        double jackpotChance = std::min(0.20, (0.01 + 0.005 * g.magnet) * fx.odds);
        double bonusChance   = std::min(0.70, (0.06 + 0.04  * g.magnet) * fx.odds);
        double swing = base * 1.5;
        if (r < jackpotChance)                     swing += base * (80.0 + 20.0 * g.magnet);
        else if (r < jackpotChance + bonusChance)  swing += base * (8.0 + 4.0 * g.magnet);
        return toTokens(swing * resonantBotPower(g) * fx.mining);
    }

    double gain = baseYield(g) * (0.6 + 0.2 * g.bot);

    std::uniform_real_distribution<double> uni(0.0, 1.0);
    double bonusChance = std::min(0.35, (0.03 + 0.02 * g.magnet) * fx.odds);
    if (uni(rng) < bonusChance) gain += baseYield(g) * (4.0 + 2.0 * g.magnet);

    return toTokens(gain * fx.mining);
}

static void autoBuy(GameState& g, MessageLog& log) {
    auto ups = upgrades(g);
    auto cheapest = std::min_element(ups.begin(), ups.end(),
        [](const UpgradeRef& a, const UpgradeRef& b) { return a.cost < b.cost; });

    if (g.tokens < cheapest->cost * AUTOBUY_RESERVE) return;

    g.tokens -= cheapest->cost;
    *cheapest->lvl += 1;
    log.push(Sev::Info, std::string("Auto-Buyer bought ") + cheapest->name + " -> lvl " +
                        std::to_string(*cheapest->lvl) + " (cost " + num(cheapest->cost) + ")");
}

void automationTick(GameState& g, MessageLog& log, std::time_t t, std::mt19937& rng, AutomationClocks& c) {
    if (g.bot > 0 && ++c.bot >= botInterval(g.bot)) {
        c.bot = 0;
        long long gain = botMine(g, t, rng);
        earn(g, gain);
        g.botTotal = addTokens(g.botTotal, gain);
        c.minedSinceReport = addTokens(c.minedSinceReport, gain);
    }

    if (++c.report >= BOT_REPORT_SECONDS) {
        c.report = 0;
        if (c.minedSinceReport > 0) {
            log.push(Sev::Info, "Mining Bot mined +" + num(c.minedSinceReport) +
                                " tokens in the last " + std::to_string(BOT_REPORT_SECONDS) + "s.");
            c.minedSinceReport = 0;
        }
    }

    if (g.autoUnlocked && g.autoOn && ++c.buy >= AUTOBUY_SECONDS) {
        c.buy = 0;
        autoBuy(g, log);
    }
}
