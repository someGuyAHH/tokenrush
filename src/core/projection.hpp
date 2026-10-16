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
