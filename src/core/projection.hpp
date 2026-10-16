#pragma once
// Where the game is heading if you change nothing: the state `t` seconds from now, from your
// current rates. `predict` reads it forward ("what will it be in 1h") and backward ("when will
// this be true"), so the two always agree.
//
// What moves: tokens and earned (your steady income: the last minute's, contract payouts aside),
// shards (Scouts at their odds once they're home, the Lucky Penny, what expeditions typically
// bring back, minus the wages on each payday), heat (its cooling schedule), the payday clock,
// crew coming home, and hand mines and jackpots (at your mining pace). Everything else stays as
// it is: nothing gets bought, and no events, contracts, disasters or offers happen.

#include "state.hpp"

struct Projection {
    double tokensPerSec = 0;   // steady income
    double minesPerSec = 0;    // your mining pace
    double pennyPerSec = 0;    // Lucky Penny shards
    std::vector<double> scoutPerSec;   // by crew slot (0 for non-Scouts)
};

Projection projectionOf(const GameState& g);
// `out` must start as a copy of `now`; only the fields that move are rewritten, so it can be
// reused for many `t` without copying the whole state again.
void projectInto(const GameState& now, const Projection& p, long long t, GameState& out);
bool movesWithTime(const std::string& value);   // tokens, shards, heat, payday, home, away, earned, mines, jackpots

// ----- With your tasks and the Auto-Buyer -----
// A task or the Auto-Buyer that's on spends as it goes, and what it buys changes the rates, so a
// formula can't say where that leads. Forecast plays it forward a second at a time instead: the
// same rates as projectInto, with your tasks and the Auto-Buyer running against the state as it
// would be (paying their fees, buying what they'd buy). What they buy changes the income from then
// on, source by source: each of mining by hand, Drones, the Bot and crew Miners scales with its own
// formula (so Pickaxe levels do nothing for a player who doesn't mine by hand). Still no events, contracts, disasters or offers, and the price only moves
// when a task trades.
bool automationOn(const GameState& g);   // a task that's on (and licensed), or the Auto-Buyer

class Forecast {
public:
    explicit Forecast(const GameState& now);
    void step();                                       // one second
    void runTo(long long t) { while (t_ < t) step(); }
    long long seconds() const { return t_; }
    GameState& state() { return s_; }                 // (conditionHolds wants it non-const)
    long long autoBuys = 0, autoSpent = 0;             // what the Auto-Buyer bought, and for how much
    double rate() const { return rate_; }              // tokens/s now

private:
    void rerate();
    GameState s_;
    std::array<double, SOURCE_COUNT> by0_{};          // the measured rate by source, at the start
    std::array<double, SOURCE_COUNT> formula0_{};     // ...and each source's formula then
    double rate0_ = 0, estimate0_ = 1, income0_ = 0;
    double rate_ = 0, minesPerSec_ = 0, penny_ = 0;
    std::vector<double> scout_;
    double tokAcc_ = 0, mineAcc_ = 0, jackAcc_ = 0, shardAcc_ = 0;
    int autoClock_ = 0;
    long long t_ = 0;
    MessageLog sink_;                                  // what the tasks would have logged
};
