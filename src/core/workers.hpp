#pragma once
// Background threads. Each one wakes once per second, takes Shared::m, runs its tick
// (see ticks.hpp, market.hpp, crew.hpp) and lets go. They never print: anything the
// player should see goes through Shared::log.

#include <stop_token>

#include "state.hpp"

void worldThread(std::stop_token st, Shared& s);       // passive income, events, contracts, heat, goals
void marketThread(std::stop_token st, Shared& s);      // the shard price
void automationThread(std::stop_token st, Shared& s);  // Mining Bot and Auto-Buyer
void crewThread(std::stop_token st, Shared& s);        // your hires
void autosaveThread(std::stop_token st, Shared& s);    // saves every AUTOSAVE_SECONDS
void exchangeThread(std::stop_token st, Shared& s);    // pay and borrow between running profiles (exchange.hpp)
