#include "projection.hpp"

#include <algorithm>
#include <cmath>

#include "contracts.hpp"
#include "crew.hpp"
#include "game.hpp"
#include "tasks.hpp"
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

bool automationOn(const GameState& g) {
    if (g.autoUnlocked && g.autoOn) return true;
    if (g.taskSlots == 0 || !isUnlocked(g, Feature::Tasks)) return false;
    for (size_t i = 0; i < g.tasks.size() && (int)i < g.taskSlots; ++i)
        if (g.tasks[i].on && g.tasks[i].error.empty()) return true;
    return false;
}

namespace {

// Each income source's formula: what scales it when levels or crew change (0: nothing does).
std::array<double, SOURCE_COUNT> sourceFormulas(const GameState& g) {
    std::array<double, SOURCE_COUNT> f{};
    f[(size_t)Source::Mining] = expectedMine(g);
    f[(size_t)Source::Drones] = passiveRatePerSec(g);
    f[(size_t)Source::Bot] = botRatePerSec(g);
    double crew = 0.0;
    for (const CrewMember& c : g.crew)
        if (c.role == Role::Miner && c.away == 0) crew += expectedMine(g) * (0.15 + 0.10 * c.level) / 5.0;   // as incomeEstimate
    f[(size_t)Source::Crew] = crew;
    return f;
}

}  // namespace

Forecast::Forecast(const GameState& now) : s_(now) {
    Projection p = projectionOf(now);
    rate0_ = p.tokensPerSec;
    by0_ = recentSourceRates(now);
    by0_[(size_t)Source::Contracts] = 0;   // (projectionOf leaves payouts out too)
    formula0_ = sourceFormulas(now);
    estimate0_ = std::max(1e-9, incomeEstimate(now));
    income0_ = now.incomeRate;
    minesPerSec_ = p.minesPerSec;
    rerate();
}

// What the rates are after the tasks or the Auto-Buyer changed something (levels, crew, the Orrery).
void Forecast::rerate() {
    double measured = 0;
    for (double r : by0_) measured += r;
    if (measured <= 0) {   // nothing measured by source yet (just loaded): the whole rate, by the overall formula
        rate_ = rate0_ * incomeEstimate(s_) / estimate0_;
    } else {
        std::array<double, SOURCE_COUNT> f = sourceFormulas(s_);
        rate_ = 0;
        for (size_t i = 0; i < (size_t)SOURCE_COUNT; ++i) {
            if (formula0_[i] > 0) rate_ += by0_[i] * f[i] / formula0_[i];
            else rate_ += by0_[i] + (i == (size_t)Source::Mining ? 0.0 : f[i]);   // a new source (your first Bot): its formula
        }
    }
    s_.incomeRate = rate0_ > 0 ? income0_ * rate_ / rate0_ : income0_;   // what tasks read as `income`; their fees follow
    Projection p = projectionOf(s_);
    penny_ = p.pennyPerSec;
    scout_ = p.scoutPerSec;
}

void Forecast::step() {
    GameState& g = s_;
    ++t_;
    g.playSeconds += 1;
    bool changed = false;

    auto whole = [](double& acc) {   // the whole part of an accumulator, taken out of it
        double w = std::floor(acc);
        acc -= w;
        return w;
    };
    tokAcc_ += rate_;
    if (long long w = toTokens(whole(tokAcc_)); w > 0) {
        g.tokens = addTokens(g.tokens, w);
        g.lifetimeEarned = addTokens(g.lifetimeEarned, w);
    }
    mineAcc_ += minesPerSec_;
    jackAcc_ += minesPerSec_ * jackpotChance(g);
    g.manualMines += (long long)whole(mineAcc_);
    g.jackpots += (long long)whole(jackAcc_);

    // Shards: the Penny, Scouts who are home, loot as crew get back; wages on payday.
    double found = penny_;
    for (size_t i = 0; i < g.crew.size(); ++i) {
        CrewMember& c = g.crew[i];
        if (c.away > 0) {
            if (--c.away == 0) {
                g.shards += expeditionLoot(g, c, c.trip);
                changed = true;   // back at work
            }
        } else if (i < scout_.size()) {
            found += scout_[i];
        }
    }
    shardAcc_ += found;
    g.shards += (long long)whole(shardAcc_);
    if (!g.crew.empty() && --g.paydayClock <= 0) {
        g.shards -= totalWages(g);
        g.paydayClock = PAYDAY_SECONDS;
    }
    if (g.heat > 0 && !infected(g, Strain::Fever) && --g.heatClock <= 0) {
        g.heat -= 1;
        g.heatClock = heatCoolSeconds(g);
    }

    // Then what runs on its own, as the real game does each second.
    long long runs = 0;
    for (const Task& t : g.tasks) runs += t.runs;
    tasksTick(g, sink_);
    for (const Task& t : g.tasks) runs -= t.runs;
    changed |= runs != 0;
    if (g.autoUnlocked && g.autoOn && ++autoClock_ >= AUTOBUY_SECONDS) {
        autoClock_ = 0;
        long long cost = 0;
        if (autoBuyOnce(g, cost)) {
            autoBuys += 1;
            autoSpent = addTokens(autoSpent, cost);
            changed = true;
        }
    }
    if (changed) rerate();
}
