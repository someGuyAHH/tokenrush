#include "projection.hpp"

#include <algorithm>
#include <cmath>

#include "contracts.hpp"
#include "crew.hpp"
#include "game.hpp"
#include "util.hpp"

Projection projectionOf(const GameState& g) {
    Projection p;
    auto by = recentSourceRates(g);
    for (int i = 0; i < SOURCE_COUNT; ++i) if (i != (int)Source::Contracts) p.tokensPerSec += by[(size_t)i];
    if (p.tokensPerSec <= 0) p.tokensPerSec = g.incomeRate;   // no minute measured yet (just loaded)
    p.minesPerSec = g.mineRate;
    if (gadgetOn(g, Gadget::LuckyPenny)) {
        double rolls = g.mineRate + (hasResonance(g) && g.bot > 0 && !g.botBroken ? 1.0 / botIntervalOf(g) : 0.0);
        p.pennyPerSec = rolls * jackpotChance(g) * 2.0;
    }
    for (const CrewMember& c : g.crew) {
        CrewMember home = c;
        home.away = 0;   // how good they'll be once they're back
        p.scoutPerSec.push_back(c.role == Role::Scout ? scoutChance(g, home) * (scoutMinFind(g, home) + scoutMaxFind(g, home)) / 2.0 / SCOUT_EVERY : 0.0);
    }
    return p;
}

void projectInto(const GameState& now, const Projection& p, long long t, GameState& out) {
    double dt = (double)std::max(0LL, t);
    out.playSeconds = now.playSeconds + t;
    out.tokens = addTokens(now.tokens, toTokens(p.tokensPerSec * dt));
    out.lifetimeEarned = addTokens(now.lifetimeEarned, toTokens(p.tokensPerSec * dt));
    out.manualMines = now.manualMines + (long long)std::floor(p.minesPerSec * dt);
    out.jackpots = now.jackpots + (long long)std::floor(p.minesPerSec * dt * jackpotChance(now));

    // Shards: Scouts once they're home, expedition loot when they get back, the Penny, minus paydays.
    double shards = (double)now.shards + p.pennyPerSec * dt;
    for (size_t i = 0; i < now.crew.size() && i < out.crew.size(); ++i) {
        const CrewMember& c = now.crew[i];
        long long back = isAway(c) ? c.away : 0;
        if (i < p.scoutPerSec.size()) shards += p.scoutPerSec[i] * (double)std::max(0LL, t - back);
        if (isAway(c) && t >= back) shards += (double)expeditionLoot(now, c, c.trip);
        out.crew[i].away = (int)std::max(0LL, (long long)c.away - t);
    }
    if (!now.crew.empty()) {
        long long paydays = t >= now.paydayClock ? 1 + (t - now.paydayClock) / PAYDAY_SECONDS : 0;
        shards -= (double)(paydays * totalWages(now));
        long long since = t - now.paydayClock;   // the clock after the paydays in between
        out.paydayClock = since < 0 ? (int)(now.paydayClock - t) : (int)(PAYDAY_SECONDS - since % PAYDAY_SECONDS);
    }
    out.shards = (long long)std::floor(shards);   // can go below 0: the crew would walk before that

    // Heat cools a point at a time.
    if (now.heat > 0 && !infected(now, Strain::Fever)) {
        int cool = heatCoolSeconds(now);
        long long drops = t >= now.heatClock ? 1 + (t - now.heatClock) / cool : 0;
        out.heat = (int)std::max(0LL, now.heat - drops);
        long long since = t - now.heatClock;
        out.heatClock = since < 0 ? (int)(now.heatClock - t) : (int)(cool - since % cool);
    }
}

bool movesWithTime(const std::string& v) {
    for (const char* m : {"tokens", "shards", "heat", "payday", "home", "away", "earned", "mines", "jackpots"})
        if (v == m) return true;
    return false;
}
