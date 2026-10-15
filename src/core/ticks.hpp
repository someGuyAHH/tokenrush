#pragma once
// One second of the game world, split by system. The worker threads call these once per
// second while holding Shared::m; the simulator (tools/sim.cpp) calls them in a fast loop.
// None of them sleep, lock, print or read the clock themselves: time comes in as `t`.

#include <ctime>
#include <random>

#include "state.hpp"

// Passive income, play time, streak expiry, random events, contracts and heat, goals.
void worldTick(GameState& g, MessageLog& log, std::time_t t, std::mt19937& rng);

// The Mining Bot and the Auto-Buyer keep a few counters between ticks.
struct AutomationClocks {
    int bot = 0;
    int buy = 0;
    int report = 0;
    long long minedSinceReport = 0;
};
void automationTick(GameState& g, MessageLog& log, std::time_t t, std::mt19937& rng, AutomationClocks& c);
